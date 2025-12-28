// mmu.c
#include "mmu.h"

#include <stdint.h>

#include "sysreg.h"

/* The Phase 3 bootstrap is the single early-MMU entry point when building
 * the boot image. In the higher-half kernel image the bootstrap has
 * already executed, so these functions become no-ops. Guard calls to
 * mmu_bootstrap with BOOT_STAGE to avoid unresolved symbols when
 * linking the kernel. */
#ifdef BOOT_STAGE
extern void mmu_bootstrap(void);
#endif

void mmu_early_enable(void) {
#ifdef BOOT_STAGE
    mmu_bootstrap();
#else
    /* No-op in the runtime kernel: MMU is already enabled by the boot stage. */
#endif
}

void mmu_kernel_init_global(void) {
#ifdef BOOT_STAGE
    mmu_bootstrap();
#else
    /* No-op in the runtime kernel.  The boot stage has already set up
     * global mappings. */
#endif
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

