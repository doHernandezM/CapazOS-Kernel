//
//  crt0.c
//  Capaz
//
//  Phase 3: crt0 runs after boot_higherhalf has enabled MMU and jumped to higher-half.
//  Responsibilities here:
//    - clear .bss (higher-half addresses)
//    - init UART (kernel MMIO VA mapped in TTBR1)
//    - optional kernel tests
//    - enable caches (optional)
//    - enter kmain
//

#include <stdint.h>
#include <stddef.h>

#include "uart_pl011.h"
#include "linker_symbols.h"
#include "mmu.h"
#include "vm_layout.h"
#include "kernel_test.h"

extern void *memset(void*, int, size_t);
extern void kmain(void);

void crt0(void) {
    // Clear kernel BSS (higher-half symbols)
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));

    /* NEW: adopt allocator cursor from boot (before TTBR0 is replaced). */
        mmu_adopt_boot_pt_allocator();

    
    // Phase 3.1: lock down TTBR0 (user space) to default-deny now that we're in TTBR1.
    mmu_ttbr0_install(mmu_ttbr0_create_minimal(), 0);

    // Switch UART to the kernel MMIO VA (mapped in TTBR1)
    uart_set_base(KERNEL_MMIO_UART0_BASE);
    uart_init();
    uart_puts("====================\n");
    uart_puts("C runtime is up (higher-half)\n");

#if KTEST_ENABLE
    ktest_run_stage(KTEST_STAGE_POST_MMU);
#endif

    // Caches are optional; enable once stable.
    uart_puts("MMU: enabling caches\n");
    mmu_enable_caches();
    uart_puts("MMU: caches enabled\n");

#if KTEST_ENABLE
    ktest_run_stage(KTEST_STAGE_POST_CACHE);
#endif

    kmain();
}
