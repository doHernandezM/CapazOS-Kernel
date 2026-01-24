/*
 * kmain.c
 *
 * Kernel entry point. At this stage we are in EL1 with the MMU enabled and a
 * high-half stack established. The boot stage passes a boot_info_t pointer in x0.
 */

#include <stdint.h>
#include <stdbool.h>

// Canonical boundary contract (Kernel <-> Core)
#include "core_kernel_abi.h"

#include "boot_info.h"
#include "build_info.h"
#include "dtb.h"
#include "mmu.h"
#include "pmm.h"
#include "platform.h"
#include "uart_pl011.h"
#include "irq.h"
#include "preempt.h"
#include "gicv2.h"
#include "timer_generic.h"
#include "work/work_queue.h"
#include "sched.h"
#include "kheap.h"   // kbuf_alloc/kbuf_free (buffer-tier allocator)
#include "panic.h"   // panic()

// Core entrypoints are declared in core_entrypoints.h (included via
// core_kernel_abi.h). A weak stub implementation is provided by
// core_entrypoints_stub.c so the Kernel can build/link without Core.
const kernel_services_v1_t *kernel_services_v1(void);

#include "config.h"

#include "MathHelper.h"
#include "sched/thread.h"
#include "ipc/ipc_message.h"
#include "cap/cap_entry.h"
#include "cap/cap_table.h"
#include "cap/cap_ops.h"
#include "ipc/ipc_selftest.h"
#include "ipc/endpoint.h"
#include "task/task.h"

/*
 * Enable/disable noisy early-boot diagnostics.
 * - Default follows the build's DEBUG macro.
 * - Override with -DKMAIN_DEBUG=0/1.
 *
 *#define DEBUG = 0
 */
 
#ifndef KMAIN_DEBUG
#  ifdef DEBUG
#    define KMAIN_DEBUG 1
#  else
#    define KMAIN_DEBUG 0
#  endif
#endif

static void print_total_memory_from_dtb(void)
{
    dtb_range_t ranges[DTB_MAX_MEMORY_RANGES];

    uint32_t count = DTB_MAX_MEMORY_RANGES;   // capacity IN
    if (!dtb_get_memory_ranges(ranges, &count) || count == 0) {
        return;
    }

    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        total += ranges[i].size;
    }

    char buf[32];
    mh_format_bytes_pretty(buf, sizeof(buf), total);
    uart_puts("Memory: ");
    uart_puts(buf);
    uart_putnl();

}

#if KMAIN_DEBUG
static void pmm_print_free_total(const char *label) {
    uint64_t free_pages = 0, total_pages = 0;
    if (!pmm_get_stats(&free_pages, &total_pages)) {
        uart_puts(label);
        uart_puts("(free/total): <uninitialized>\n");
        return;
    }
    uart_puts(label);
    uart_puts("(free/total): ");
    uart_putu64_dec(free_pages);
    uart_putc('/');
    uart_putu64_dec(total_pages);
    uart_putnl();
}

static void pmm_quick_alloc_test(void) {
    uart_puts("PMM\n");
    pmm_print_free_total("Start");

    /* Simple allocate/free cycles using a fixed stack buffer of PAs. */
    enum { N = 1024 };
    uint64_t pages[N];
    uint32_t allocated = 0;

    /* Cycle 1: allocate up to N pages. */
    for (uint32_t i = 0; i < (uint32_t)N; i++) {
        if (!pmm_alloc_page(&pages[i])) break;
        allocated++;
    }
    uart_puts("Alloc1: "); uart_putu64_dec(allocated); uart_puts(" pages\n");
    pmm_print_free_total("AfterAlloc1");

    /* Free every other page. */
    uint32_t freed = 0;
    for (uint32_t i = 0; i < allocated; i += 2) {
        pmm_free_page(pages[i]);
        freed++;
        pages[i] = 0;
    }
    uart_puts("Free1: "); uart_putu64_dec(freed); uart_puts(" pages\n");
    pmm_print_free_total("AfterFree1");

    /* Cycle 2: try a contiguous allocation (64 pages = 256KiB). */
    uint64_t run_pa = 0;
    if (pmm_alloc_pages(64, &run_pa)) {
        uart_puts("Alloc2: contiguous 64 pages at "); uart_puthex64(run_pa); uart_putnl();
    } else {
        uart_puts("Alloc2: contiguous 64 pages failed\n");
    }
    pmm_print_free_total("AfterAlloc2");

    if (run_pa) {
        for (uint32_t i = 0; i < 64; i++) {
            pmm_free_page(run_pa + ((uint64_t)i * 0x1000ULL));
        }
        uart_puts("Free2: contiguous 64 pages\n");
        pmm_print_free_total("AfterFree2");
    }

    /* Free remaining pages from cycle 1. */
    for (uint32_t i = 0; i < allocated; i++) {
        if (pages[i]) pmm_free_page(pages[i]);
    }
    pmm_print_free_total("End");
}
#endif

/*
 * Called from the EL1 exception vectors (kernel_vectors.S).
 * Prints minimal EL1 fault state and parks the CPU.
 */
__attribute__((used))
void kernel_exception_report(uint64_t esr, uint64_t far, uint64_t elr,
                             uint64_t sp, const uint64_t *regs)
{
    uart_puts("\n*** EL1 EXCEPTION ***\n");
    uart_puts("ESR_EL1="); uart_puthex64(esr); uart_putnl();
    uart_puts("FAR_EL1="); uart_puthex64(far); uart_putnl();
    uart_puts("ELR_EL1="); uart_puthex64(elr); uart_putnl();
    uart_puts("SP_EL1 ="); uart_puthex64(sp);  uart_putnl();

    if (regs) {
        uart_puts("x0     ="); uart_puthex64(regs[0]);  uart_putnl();
        uart_puts("x1     ="); uart_puthex64(regs[1]);  uart_putnl();
        uart_puts("x2     ="); uart_puthex64(regs[2]);  uart_putnl();
        uart_puts("x3     ="); uart_puthex64(regs[3]);  uart_putnl();
        uart_puts("x29(fp)="); uart_puthex64(regs[29]); uart_putnl();
        uart_puts("x30(lr)="); uart_puthex64(regs[30]); uart_putnl();
    }

    for (;;) {
        __asm__ volatile("wfi");
    }
}


/* M6: Deferred work queue (IRQ top-half only). */
static workq_t g_deferred_workq;
static task_t g_kernel_task;
static cap_table_t g_kernel_cap_table;
static uint32_t g_timer_token;
// NOTE: In the current M7 skeleton we seed capabilities for the log service
// (and other bootstrap objects) directly into the kernel cap table. There is no
// separate global "token" value to track here, and leaving an unused global
// trips -Werror/-Wunused-variable under the Xcode build.
static volatile bool g_tick_work_pending = false;

static void tick_work_fn(void *arg);

/* Preallocated tick work item: never freed. */
static work_item_t g_tick_item = { .fn = tick_work_fn, .arg = NULL, .next = NULL };

static void tick_work_fn(void *arg)
{
    (void)arg;
    /* Mark as no longer pending before doing any work. */
    g_tick_work_pending = false;
    /* Defer scheduler signal out of IRQ context. */
    preempt_set_need_resched();
}

/* Timer IRQ handler (allocation-free): ack + enqueue work. */
static void timer_irq_handler(uint32_t irq, void *ctx, trap_frame_t *tf)
{
    (void)irq; (void)ctx; (void)tf;

    /* Top-half: acknowledge/re-arm the timer. */
    timer_handle_irq();

    /* Enqueue the deferred tick work item (no allocation in IRQ). */
    if (!g_tick_work_pending) {
        g_tick_work_pending = true;
        (void)workq_enqueue_from_irq(&g_deferred_workq, &g_tick_item);
    }
}

/*
 * M6: Dedicated Core thread.
 * - Calls core_main() exactly once (if present).
 * - Drains deferred work queue in thread context.
 */
static void core_thread_entry(void *arg)
{
    (void)arg;

    /* M7: Seed initial caps for kernel task in core/main thread entry (before core_main()). */
    cap_table_init(&g_kernel_cap_table);
    task_init(&g_kernel_task, 0, &g_kernel_cap_table);

    cap_status_t st;

    st = cap_create(&g_kernel_cap_table,
                    CAP_TYPE_TASK,
                    (cap_rights_t)(CAP_R_DUP | CAP_R_TRANSFER | CAP_R_CONTROL),
                    &g_kernel_task,
                    &g_kernel_task.self_cap);
    if (st != CAP_OK) {
        panic("core/main: failed to seed task cap");
    }

    /* Placeholder token objects for M7 (mechanism labels only). */
    g_timer_token = 1;
    st = cap_create(&g_kernel_cap_table,
                    CAP_TYPE_TIMER_TOKEN,
                    (cap_rights_t)(CAP_R_ARM | CAP_R_ACK | CAP_R_DUP | CAP_R_TRANSFER),
                    &g_timer_token,
                    &g_kernel_task.timer_cap);
    if (st != CAP_OK) {
        panic("core/main: failed to seed timer cap");
    }

    st = cap_create(&g_kernel_cap_table,
                    CAP_TYPE_SERVICE,
                    (cap_rights_t)(CAP_R_READ | CAP_R_DUP | CAP_R_TRANSFER),
                    (void *)kernel_services_v1(),
                    &g_kernel_task.log_cap);
    if (st != CAP_OK) {
        panic("core/main: failed to seed log service cap");
    }

#ifdef DEBUG
    cap_ops_selftest(&g_kernel_cap_table);
#endif

    /* Phase 2 contract: Core runs once in this thread. */
    // Hand services table to Core, then enter Core.
    // If Core is not linked, weak stubs (in core_entrypoints_stub.c) make this a no-op.
    core_set_services(kernel_services_v1());
    (void)core_main();

    for (;;) {
        /* Drain all pending work items. */
        for (;;) {
            work_item_t *it = workq_dequeue(&g_deferred_workq);
            if (!it) break;

            it->fn(it->arg);

            /* Free cached items; the tick item is preallocated/static. */
            if (it != &g_tick_item) {
                work_item_free(it);
            }
        }

        /* Cooperative scheduling hook for the current stage. */
        if (preempt_need_resched()) {
            preempt_clear_need_resched();
            yield();
            continue;
        }

        /* Sleep until the next interrupt enqueues more work. */
        __asm__ volatile ("wfi");
    }
}

void kmain(const boot_info_t *boot_info)
{
    /* Ensure we have a working UART even before DTB parsing. */
    uart_init(0);

    uart_puts("Kernel: ");
    uart_puts(CAPAZ_KERNEL_VERSION);
    uart_putnl();

    uart_puts("Machine: ");
    uart_puts(CAPAZ_MACHINE);
    uart_putnl();


    
#if KMAIN_DEBUG
    if (boot_info) {
        uart_puts("boot_info: kernel_pa="); uart_puthex64(boot_info->kernel_phys_base);
        uart_puts(" size="); uart_puthex64(boot_info->kernel_size);
        uart_puts(" entry_off="); uart_puthex64(boot_info->kernel_entry_offset);
        uart_putnl();

        uart_puts("boot_info: dtb_va="); uart_puthex64(boot_info->dtb_ptr);
        uart_puts(" dtb_size="); uart_puthex64(boot_info->dtb_size);
        uart_putnl();
    }
#endif

    /* DTB bring-up: validate and print what we can. */
    if (boot_info && boot_info->dtb_ptr != 0) {
        if (dtb_init((const void *)(uintptr_t)boot_info->dtb_ptr, boot_info->dtb_size)) {

#if KMAIN_DEBUG
            dtb_dump_summary();
#endif

            /* If DTB gives us a UART base, switch to it (fallback otherwise). */
            uint64_t uart_phys = 0;
            if (dtb_find_pl011_uart(&uart_phys)) {
#if KMAIN_DEBUG
                uart_puts("UART: switching to DTB base "); uart_puthex64(uart_phys); uart_putnl();
#endif
                uart_init(uart_phys);
                uart_puts("UART: "); uart_puthex64(uart_phys); uart_putnl();
            }

            /* Derive allocator-friendly usable RAM spans (RAM - reserved - implicit). */
#if KMAIN_DEBUG
            platform_dump_memory_map(boot_info);
#endif
        } else {
            uart_puts("DTB: invalid header (fallback to hardcoded UART)\n");
        }
    } else {
        uart_puts("DTB: no pointer provided (fallback to hardcoded UART)\n");
    }

    /* Install kernel page tables (TTBR1) and disable TTBR0. */
    mmu_init(boot_info);
#if defined(CAPAZ_FAULT_TEST) && (CAPAZ_FAULT_TEST)
    uart_puts("CAPAZ_FAULT_TEST: triggering deliberate exception (BRK)\n");
    __asm__ volatile("brk #0");
#endif

    
    /* Initialize bitmap PMM using TTBR1 high-half direct map. */
    pmm_init(boot_info);

#if KMAIN_DEBUG
    /* Quick sanity test: allocate/free cycles and print free/total. */
    pmm_quick_alloc_test();
#endif
    
    /* Always print a short, stable summary. */
    print_total_memory_from_dtb();

    // Treat kmain() as the bootstrap "current thread" even if we haven't
    // created any kernel threads yet. This keeps the IRQ-exit scaffolding
    // (M8 readiness) safe and avoids panics on the first timer tick.
    // M5.5: Initialize slab caches for high-churn kernel objects.
    thread_alloc_init();
    ipc_msg_cache_init();
    endpoint_cache_init();
    cap_entry_cache_init();
    /* M6: Work item cache + deferred work queue. */
    work_item_cache_init();
    workq_init(&g_deferred_workq);

    sched_init_bootstrap();
    // M7: cap-space is initialized and seeded in core/main thread entry (before core_main).

    /* M6: Bring up interrupts + timer tick after core init. */
    irq_global_disable();
    gicv2_init();

    /* Register and enable the architected timer interrupt. */
    (void)irq_register(TIMER_PPI_IRQ, timer_irq_handler, 0);
    /*
     * Generic timer PPIs are level-sensitive. Configuring them as edge can
     * cause missed acks / repeated wakeups depending on the model.
     */
    gicv2_config_irq(TIMER_PPI_IRQ, false);
    gicv2_enable_irq(TIMER_PPI_IRQ);

    /* 100Hz tick (10ms). */
    /*
     * Start the periodic tick (unless built in tickless mode).
     * Timer IRQ handling remains registered for one-shot deadlines.
     */
    timer_init_hz(CONFIG_TICK_HZ);

    /* M6: Create and enqueue a dedicated Core thread. */
    thread_t *core_thr = thread_create_named("core/main", core_thread_entry, NULL);
    if (!core_thr) {
        uart_puts("kmain: failed to create core thread\n");
        for (;;) {
            __asm__ volatile ("wfi");
        }
    }
    core_thr->task = &g_kernel_task;
    sched_enqueue(core_thr);

    irq_global_enable();

    uart_puts("Build: ");
    uart_putu64_dec(CAPAZ_BUILD_NUMBER);
    uart_puts("  ");
    uart_puts(CAPAZ_BUILD_DATE);
    uart_putnl();
    
    /* Enter the cooperative scheduler. */
    yield();

    /* Bootstrap thread becomes the idle thread. */
    for (;;) {
        __asm__ volatile ("wfi");
        /* Give other runnable threads a chance to run. */
        yield();
    }
}