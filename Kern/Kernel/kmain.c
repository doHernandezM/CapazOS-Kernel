/*
 * kmain.c
 *
 * Kernel entry point. At this stage we are in EL1 with the MMU enabled and a
 * high-half stack established. The boot stage passes a boot_info_t pointer in x0.
 */

#include <stdint.h>
#include <stdbool.h>

// Core ↔ Kernel internal API.  The ABI service table has been removed.
// Core calls kernel mechanisms directly via core_kernel_api.h.
// Do not include the old core_kernel_abi.h; it no longer exists.
#include "api/core_kernel_api.h"
#include "api/core_boot_if.h"
#include "api/uart_proto.h"
#include "api/intent_proto.h"
#include "api/kernel_log_proto.h"

#include "boot_info.h"
#include "buildinfo.h"
#include "dtb.h"
#include "mmu.h"
#include "pmm.h"
#include "platform.h"
#include "serial/uart.h"
#include "irq.h"
#include "preempt.h"
#include "mm/mem.h"   // memcpy
#include "gicv2.h"
#include "timer_generic.h"
#include "work/work_queue.h"
#include "sched.h"
#include "kheap.h"   // kbuf_alloc/kbuf_free (buffer-tier allocator)
#include "panic.h"   // panic()
#include "dev/virtio_blk.h"

// Core entrypoints are declared in Core (core_main and Swift).  We no longer
// expose kernel service tables.  The kernel no longer provides
// kernel_services_v1() or kernel_services_v3().

#include "config.h"

#include "MathHelper.h"
#include "sched/thread.h"
#include "ipc/ipc_message.h"
#include "ipc/endpoint.h"
#include "cap/cap_entry.h"
#include "cap/cap_table.h"
#include "cap/cap_ops.h"
#include "ipc/ipc_selftest.h"
#include "task/task.h"
#include "debug/klog.h"

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
    klog_puts("Memory: ");
    klog_puts(buf);
    klog_putnl();

}

#if KMAIN_DEBUG
static void pmm_print_free_total(const char *label) {
    uint64_t free_pages = 0, total_pages = 0;
    if (!pmm_get_stats(&free_pages, &total_pages)) {
        klog_puts(label);
        klog_puts("(free/total): <uninitialized>\n");
        return;
    }
    klog_puts(label);
    klog_puts("(free/total): ");
    klog_putu64_dec(free_pages);
    klog_putc('/');
    klog_putu64_dec(total_pages);
    klog_putnl();
}

static void pmm_quick_alloc_test(void) {
    klog_puts("PMM\n");
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
    klog_puts("Alloc1: "); klog_putu64_dec(allocated); klog_puts(" pages\n");
    pmm_print_free_total("AfterAlloc1");

    /* Free every other page. */
    uint32_t freed = 0;
    for (uint32_t i = 0; i < allocated; i += 2) {
        pmm_free_page(pages[i]);
        freed++;
        pages[i] = 0;
    }
    klog_puts("Free1: "); klog_putu64_dec(freed); klog_puts(" pages\n");
    pmm_print_free_total("AfterFree1");

    /* Cycle 2: try a contiguous allocation (64 pages = 256KiB). */
    uint64_t run_pa = 0;
    if (pmm_alloc_pages(64, &run_pa)) {
        klog_puts("Alloc2: contiguous 64 pages at "); klog_puthex64(run_pa); klog_putnl();
    } else {
        klog_puts("Alloc2: contiguous 64 pages failed\n");
    }
    pmm_print_free_total("AfterAlloc2");

    if (run_pa) {
        for (uint32_t i = 0; i < 64; i++) {
            pmm_free_page(run_pa + ((uint64_t)i * 0x1000ULL));
        }
        klog_puts("Free2: contiguous 64 pages\n");
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
    klog_puts("\n*** EL1 EXCEPTION ***\n");
    klog_puts("ESR_EL1="); klog_puthex64(esr); klog_putnl();
    klog_puts("FAR_EL1="); klog_puthex64(far); klog_putnl();
    klog_puts("ELR_EL1="); klog_puthex64(elr); klog_putnl();
    klog_puts("SP_EL1 ="); klog_puthex64(sp);  klog_putnl();

    if (regs) {
        klog_puts("x0     ="); klog_puthex64(regs[0]);  klog_putnl();
        klog_puts("x1     ="); klog_puthex64(regs[1]);  klog_putnl();
        klog_puts("x2     ="); klog_puthex64(regs[2]);  klog_putnl();
        klog_puts("x3     ="); klog_puthex64(regs[3]);  klog_putnl();
        klog_puts("x29(fp)="); klog_puthex64(regs[29]); klog_putnl();
        klog_puts("x30(lr)="); klog_puthex64(regs[30]); klog_putnl();
    }

    for (;;) {
        __asm__ volatile("wfi");
    }
}


/* Deferred work queue (IRQ top-half only). */
// Shared deferred work queue used by interrupt/driver code to schedule work
// that must run in thread context.
//
// This must be a global (not `static`) because some subsystems (e.g. console
// RX handlers) reference it from other translation units.
workq_t g_deferred_workq;
static task_t g_kernel_task;
static cap_table_t g_kernel_cap_table;
// Removed: g_timer_token and log service seeding.  Capabilities will be
// seeded explicitly when services are implemented.  We keep g_tick_work_pending
// to coordinate deferred tick work.
static volatile bool g_tick_work_pending = false;

static void tick_work_fn(void *arg);
static void uart_driver_pump(void);
extern __attribute__((weak)) void core_poll(void);

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

static void uart_driver_pump(void)
{
    ks_ipc_msg_t msg;
    for (;;) {
        ks_ipc_status_t st = ipc_try_recv_cap(&g_kernel_cap_table,
                                              (cap_handle_t)g_kernel_task.uart_cmd_ep_cap,
                                              &msg);
        if (st == KS_IPC_OK) {
            if (msg.tag == UART_TAG_WRITE && msg.len > 0) {
                uart_write((const char *)msg.data, (size_t)msg.len);
            } else if (msg.tag == UART_TAG_QUERY_INTENTS) {
                static const ks_intent_desc_t k_uart_intents[] = {
                    { UART_TAG_WRITE, KS_INTENT_DIR_CALL, KS_IPC_MSG_MAX, 0, CAP_R_SEND },
                    { UART_TAG_RX_EVENT, KS_INTENT_DIR_EVENT, KS_IPC_MSG_MAX, 0, CAP_R_RECV },
                    { UART_TAG_QUERY_INTENTS, KS_INTENT_DIR_CALL, 0, 0, CAP_R_SEND },
                };
                const uint32_t max_count = (uint32_t)(sizeof(k_uart_intents) / sizeof(k_uart_intents[0]));
                const uint32_t max_payload = KS_IPC_MSG_MAX;
                const uint32_t header_size = (uint32_t)sizeof(uint32_t);
                uint32_t max_fit = 0;
                if (max_payload > header_size) {
                    max_fit = (max_payload - header_size) / (uint32_t)sizeof(ks_intent_desc_t);
                }
                uint32_t count = max_count;
                if (count > max_fit) {
                    count = max_fit;
                }

                ks_ipc_msg_t resp;
                resp.tag = UART_TAG_QUERY_INTENTS;
                resp.len = header_size + count * (uint32_t)sizeof(ks_intent_desc_t);
                // Pack: [u32 count][desc...]
                memcpy(resp.data, &count, sizeof(uint32_t));
                if (count > 0) {
                    memcpy(resp.data + header_size, k_uart_intents, count * sizeof(ks_intent_desc_t));
                }
                (void)ipc_send_cap(&g_kernel_cap_table,
                                   (cap_handle_t)g_kernel_task.uart_evt_ep_cap,
                                   &resp);
            } else if (msg.tag == UART_TAG_OPEN_CONTRACT) {
                if (msg.len >= sizeof(uint32_t) * 2) {
                    uint32_t kind = 0;
                    uint32_t rights = 0;
                    memcpy(&kind, msg.data, sizeof(uint32_t));
                    memcpy(&rights, msg.data + sizeof(uint32_t), sizeof(uint32_t));

                    cap_handle_t src = CAP_HANDLE_INVALID;
                    if (kind == UART_CONTRACT_CMD) {
                        src = g_kernel_task.uart_cmd_ep_cap;
                    } else if (kind == UART_CONTRACT_EVT) {
                        src = g_kernel_task.uart_evt_ep_cap;
                    }

                    ks_status_t resp_status = KS_STATUS_INVALID_ARG;
                    cap_handle_t new_cap = CAP_HANDLE_INVALID;
                    if (src != CAP_HANDLE_INVALID) {
                        cap_status_t cap_st = cap_dup(&g_kernel_cap_table,
                                                      src,
                                                      &g_kernel_cap_table,
                                                      (cap_rights_t)rights,
                                                      &new_cap);
                        switch (cap_st) {
                            case CAP_OK:
                                resp_status = KS_STATUS_OK;
                                break;
                            case CAP_ERR_DENIED:
                                resp_status = KS_STATUS_NO_RIGHTS;
                                break;
                            case CAP_ERR_NO_MEM:
                                resp_status = KS_STATUS_OUT_OF_MEMORY;
                                break;
                            default:
                                resp_status = KS_STATUS_INVALID_ARG;
                                break;
                        }
                    }

                    ks_ipc_msg_t resp;
                    resp.tag = UART_TAG_OPEN_CONTRACT;
                    resp.len = 16;
                    memcpy(resp.data, &resp_status, sizeof(int32_t));
                    uint32_t pad = 0;
                    memcpy(resp.data + 4, &pad, sizeof(uint32_t));
                    memcpy(resp.data + 8, &new_cap, sizeof(uint64_t));
                    (void)ipc_send_cap(&g_kernel_cap_table,
                                       (cap_handle_t)g_kernel_task.uart_evt_ep_cap,
                                       &resp);
                }
            }
            continue;
        }
        if (st == KS_IPC_ERR_EMPTY) {
            break;
        }
        // Ignore other errors for now; the pump is best-effort.
        break;
    }

    // RX path: poll PL011 and post RX_EVENTs.
    char ch;
    while (uart_getc_nonblock(&ch)) {
        ks_ipc_msg_t evt;
        evt.tag = UART_TAG_RX_EVENT;
        evt.len = 1;
        evt.data[0] = (uint8_t)ch;
        (void)ipc_send_cap(&g_kernel_cap_table,
                           (cap_handle_t)g_kernel_task.uart_evt_ep_cap,
                           &evt);
    }
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
 * Dedicated Core thread.
 * - Calls core_main() exactly once (if present).
 * - Drains deferred work queue in thread context.
 */
static void core_thread_entry(void *arg)
{
    (void)arg;

    /* Seed initial caps for kernel task. */
    cap_table_init(&g_kernel_cap_table);
    task_init(&g_kernel_task, &g_kernel_cap_table);

    // Provide Core access to the kernel task's cap space through the internal API.
    cka_attach_core_caps(&g_kernel_cap_table);

    // 1) Task self-cap (used for bookkeeping and future capability-mediated task ops).
    cap_status_t st = cap_create(&g_kernel_cap_table,
                                CAP_TYPE_TASK,
                                (cap_rights_t)(CAP_R_DUP | CAP_R_DROP),
                                &g_kernel_task,
                                &g_kernel_task.self_cap);
    if (st != CAP_OK) {
        klog_puts("cap_create TASK failed st=");
        klog_putu64_dec((uint64_t)st);
        klog_puts(" free_top=");
        klog_putu64_dec((uint64_t)g_kernel_cap_table.free_top);
        klog_puts("\n");
        panic("kmain: cap_create(CAP_TYPE_TASK) failed");
    }

    // 2) UART driver endpoints (command + event).
    endpoint_t *uart_cmd_ep = NULL;
    endpoint_t *uart_evt_ep = NULL;
    endpoint_t *kernel_log_ep = NULL;
    // endpoint_create creates the endpoint object; rights are applied when creating the cap.
    ks_status_t ep_st = endpoint_create(&uart_cmd_ep);
    if (ep_st != KS_STATUS_OK || uart_cmd_ep == NULL) {
        panic("kmain: endpoint_create(uart_cmd) failed");
    }
    ep_st = endpoint_create(&uart_evt_ep);
    if (ep_st != KS_STATUS_OK || uart_evt_ep == NULL) {
        panic("kmain: endpoint_create(uart_evt) failed");
    }
    ep_st = endpoint_create(&kernel_log_ep);
    if (ep_st != KS_STATUS_OK || kernel_log_ep == NULL) {
        panic("kmain: endpoint_create(kernel_log) failed");
    }

    st = cap_create(&g_kernel_cap_table,
                    CAP_TYPE_ENDPOINT,
                    (cap_rights_t)(CAP_R_SEND | CAP_R_RECV | CAP_R_DUP | CAP_R_DROP),
                    uart_cmd_ep,
                    &g_kernel_task.uart_cmd_ep_cap);
    if (st != CAP_OK) {
        klog_puts("cap_create ENDPOINT failed st=");
        klog_putu64_dec((uint64_t)st);
        klog_puts(" free_top=");
        klog_putu64_dec((uint64_t)g_kernel_cap_table.free_top);
        klog_puts("\n");
        panic("kmain: cap_create(CAP_TYPE_ENDPOINT) failed");
    }

    st = cap_create(&g_kernel_cap_table,
                    CAP_TYPE_ENDPOINT,
                    (cap_rights_t)(CAP_R_SEND | CAP_R_RECV | CAP_R_DUP | CAP_R_DROP),
                    uart_evt_ep,
                    &g_kernel_task.uart_evt_ep_cap);
    if (st != CAP_OK) {
        klog_puts("cap_create ENDPOINT failed st=");
        klog_putu64_dec((uint64_t)st);
        klog_puts(" free_top=");
        klog_putu64_dec((uint64_t)g_kernel_cap_table.free_top);
        klog_puts("\n");
        panic("kmain: cap_create(CAP_TYPE_ENDPOINT) failed");
    }

    // KernelLog endpoint: send-only for kernel, recv-only for Core.
    st = cap_create(&g_kernel_cap_table,
                    CAP_TYPE_ENDPOINT,
                    (cap_rights_t)(CAP_R_SEND | CAP_R_DUP | CAP_R_DROP),
                    kernel_log_ep,
                    &g_kernel_task.kernel_log_send_cap);
    if (st != CAP_OK) {
        klog_puts("cap_create ENDPOINT failed st=");
        klog_putu64_dec((uint64_t)st);
        klog_puts(" free_top=");
        klog_putu64_dec((uint64_t)g_kernel_cap_table.free_top);
        klog_puts("\n");
        panic("kmain: cap_create(CAP_TYPE_ENDPOINT) failed");
    }

    st = cap_create(&g_kernel_cap_table,
                    CAP_TYPE_ENDPOINT,
                    (cap_rights_t)(CAP_R_RECV | CAP_R_DROP),
                    kernel_log_ep,
                    &g_kernel_task.kernel_log_recv_cap);
    if (st != CAP_OK) {
        klog_puts("cap_create ENDPOINT failed st=");
        klog_putu64_dec((uint64_t)st);
        klog_puts(" free_top=");
        klog_putu64_dec((uint64_t)g_kernel_cap_table.free_top);
        klog_puts("\n");
        panic("kmain: cap_create(CAP_TYPE_ENDPOINT) failed");
    }

    // Allow kernel log forwarding to target KernelLog endpoint.
    cka_attach_kernel_log_ep(g_kernel_task.kernel_log_send_cap);

    // Keep console_ep_cap as an alias to UART cmd during the transition.
    g_kernel_task.console_ep_cap = g_kernel_task.uart_cmd_ep_cap;

    // Publish the minimal boot interface to Core (A0).
    const core_boot_if_t core_if = {
        .version = 3,
        .core_caps = &g_kernel_cap_table,
        .core_task = &g_kernel_task,
        .console_ep = g_kernel_task.console_ep_cap,
        .uart_cmd_ep = g_kernel_task.uart_cmd_ep_cap,
        .uart_evt_ep = g_kernel_task.uart_evt_ep_cap,
        .kernel_log_ep = g_kernel_task.kernel_log_recv_cap,
    };
    core_boot_attach(&core_if);

    /* Contract: Core runs once in this thread. */
    (void)core_main();

    for (;;) {
        uart_driver_pump();
        if (core_poll) {
            core_poll();
        } else {
            static bool s_warned = false;
            if (!s_warned) {
                s_warned = true;
            }
        }
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

    klog_puts("Kernel: ");
    klog_puts(CAPAZ_KERNEL_VERSION);
    klog_putnl();

    klog_puts("Machine: ");
    klog_puts(CAPAZ_MACHINE);
    klog_putnl();


    
#if KMAIN_DEBUG
    if (boot_info) {
        klog_puts("boot_info: kernel_pa="); klog_puthex64(boot_info->kernel_phys_base);
        klog_puts(" size="); klog_puthex64(boot_info->kernel_size);
        klog_puts(" entry_off="); klog_puthex64(boot_info->kernel_entry_offset);
        klog_putnl();

        klog_puts("boot_info: dtb_va="); klog_puthex64(boot_info->dtb_ptr);
        klog_puts(" dtb_size="); klog_puthex64(boot_info->dtb_size);
        klog_putnl();
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
                klog_puts("UART: switching to DTB base "); klog_puthex64(uart_phys); klog_putnl();
#endif
                uart_init(uart_phys);
                klog_puts("UART: "); klog_puthex64(uart_phys); klog_putnl();
            }

            /* Derive allocator-friendly usable RAM spans (RAM - reserved - implicit). */
#if KMAIN_DEBUG
            platform_dump_memory_map(boot_info);
#endif
        } else {
            klog_puts("DTB: invalid header (fallback to hardcoded UART)\n");
        }
    } else {
        klog_puts("DTB: no pointer provided (fallback to hardcoded UART)\n");
    }

    /* Install kernel page tables (TTBR1) and disable TTBR0. */
    mmu_init(boot_info);
#if defined(CAPAZ_FAULT_TEST) && (CAPAZ_FAULT_TEST)
    klog_puts("CAPAZ_FAULT_TEST: triggering deliberate exception (BRK)\n");
    __asm__ volatile("brk #0");
#endif

    
    /* Initialize bitmap PMM using TTBR1 high-half direct map. */
    pmm_init(boot_info);

    /* Initialize kernel heap before any dynamic allocations. */
    kheap_init();

#if KMAIN_DEBUG
    /* Quick sanity test: allocate/free cycles and print free/total. */
    pmm_quick_alloc_test();
#endif
    
    /* Always print a short, stable summary. */
    print_total_memory_from_dtb();

    // Treat kmain() as the bootstrap "current thread" even if we haven't
    // created any kernel threads yet. This keeps the IRQ-exit scaffolding
    // Preemption-ready: safe and avoids panics on the first timer tick.
    // Initialize slab caches for high-churn kernel objects.
    thread_alloc_init();
    ipc_msg_cache_init();
    endpoint_cache_init();
    cap_entry_cache_init();
    /* Work item cache + deferred work queue. */
    work_item_cache_init();
    workq_init(&g_deferred_workq);

    sched_init_bootstrap();
    // Cap-space is initialized and seeded in core/main thread entry (before core_main).

    /* Initialize virtio block device (read-only bring-up). */
    (void)virtio_blk_init();

    /* Bring up interrupts + timer tick after core init. */
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

    /* Create and enqueue a dedicated Core thread. */
    thread_t *core_thr = thread_create_named("core/main", core_thread_entry, NULL);
    if (!core_thr) {
        klog_puts("kmain: failed to create core thread\n");
        for (;;) {
            __asm__ volatile ("wfi");
        }
    }
    core_thr->task = &g_kernel_task;
    sched_enqueue(core_thr);

    irq_global_enable();

    klog_puts("Build: ");
    klog_putu64_dec(CAPAZ_BUILD_NUMBER);
    klog_puts("  ");
    klog_puts(CAPAZ_BUILD_DATE);
    klog_putnl();
    
    /* Enter the cooperative scheduler. */
    yield();

    /* Bootstrap thread becomes the idle thread. */
    for (;;) {
        __asm__ volatile ("wfi");
        /* Give other runnable threads a chance to run. */
        yield();
    }
}
