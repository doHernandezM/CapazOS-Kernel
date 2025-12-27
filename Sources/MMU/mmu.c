// mmu.c
#include "mmu.h"

#include <stdint.h>

#include "sysreg.h"

/* The Phase 3 bootstrap is the single early-MMU entry point. Provide
 * compatibility names as symbol aliases (no out-of-range branches). */
extern void mmu_bootstrap(void);
void mmu_early_enable(void) {
    mmu_bootstrap();
}

void mmu_kernel_init_global(void) {
    mmu_bootstrap();
}

void mmu_enable_caches(void) {
    uint64_t sctlr = read_sctlr_el1();

    /* Enable D-cache + I-cache. */
    sctlr |= (1ull << 2);    /* C */
    sctlr |= (1ull << 12);   /* I */

    /* Keep safety bits asserted (idempotent). */
    sctlr |= (1ull << 19);   /* WXN */
    sctlr |= (1ull << 22);   /* PAN */

    write_sctlr_el1(sctlr);
    isb();
}

