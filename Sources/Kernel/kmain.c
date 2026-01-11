/*
 * kmain.c
 *
 * Kernel entry point. At this stage we are in EL1 with the MMU enabled and a
 * high-half stack established. The boot stage passes a boot_info_t pointer in x0.
 */

#include <stdint.h>

#include "boot_info.h"
#include "dtb.h"
#include "mmu.h"
#include "pmm.h"
#include "platform.h"
#include "uart_pl011.h"
#include "irq.h"
#include "preempt.h"
#include "gicv2.h"
#include "timer_generic.h"
#include "sched.h"

#include "config.h"

#include "MathHelper.h"

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


/* M6: Timer IRQ handler (allocation-free). */
static void timer_irq_handler(uint32_t irq, void *ctx, trap_frame_t *tf)
{
    (void)irq; (void)ctx; (void)tf;
    timer_handle_irq();
    preempt_set_need_resched();
}

void kmain(const boot_info_t *boot_info)
{
    /* Ensure we have a working UART even before DTB parsing. */
    uart_init(0);

    uart_puts("Kernel: 0.0.8D\nMachine: Virt\n");

    
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
    sched_init_bootstrap();


    
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

    irq_global_enable();

    /* Report tick progress from the idle loop (not from ISR). */
#if (CONFIG_TICKLESS == 0)
    uint64_t last = 0;
    for (;;) {
        __asm__ volatile ("wfi");
        uint64_t t = timer_ticks_read();
        if (t - last >= (uint64_t)CONFIG_TICK_HZ) { /* ~1s */
            last = t;
            uart_puts("tick:");
            uart_putu64_dec(t / (uint64_t)CONFIG_TICK_HZ);
            uart_puts("\n");
        }
    }
#else
    for (;;) {
        __asm__ volatile ("wfi");
    }
#endif
}
