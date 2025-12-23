//
//  crt0.c
//  OSpost
//
//  Created by Cosas on 12/20/25.
//
#include <stdint.h>
#include <stddef.h>

#include "uart_pl011.h"
#include "linker_symbols.h"   // adjust include path to match your project
#include "mmu.h"                  // adjust include path to match your project
#include "kernel_test.h"   // NEW

extern void *memset(void*, int, size_t);
extern void kmain(void);

void crt0(void) {
    memset(__bss_start, 0, (size_t)(__bss_end - __bss_start));

    uart_init();
    uart_puts("====================\n");
    uart_puts("C runtime is up\n");

#if KTEST_ENABLE
    ktest_run_stage(KTEST_STAGE_PRE_MMU);
#endif

    uart_puts("MMU: enabling (translation only)\n");
    mmu_early_enable();
    uart_puts("MMU: enabled\n");

#if KTEST_ENABLE
    ktest_run_stage(KTEST_STAGE_POST_MMU);
#endif

    uart_puts("MMU: enabling caches\n");
    mmu_enable_caches();
    uart_puts("MMU: caches enabled\n");

#if KTEST_ENABLE
    ktest_run_stage(KTEST_STAGE_POST_CACHE);
#endif
    
    kmain();
}
