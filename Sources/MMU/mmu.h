#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Phase 2 compatibility (delegates to Phase 3 bootstrap). */
void mmu_early_enable(void);
void mmu_kernel_init_global(void);

/* Phase 3 bootstrap: builds TTBR0+TTBR1 and enables the MMU.
 * Must be called from identity/boot code before branching to the higher-half.
 *
 * When building the runtime kernel image (BOOT_STAGE undefined) this
 * function is not available; the MMU is already enabled by the boot
 * stage.  Only declare the prototype when compiling for the boot
 * stage. */
#ifdef BOOT_STAGE
void mmu_bootstrap(void);
#endif

/* Runtime helpers (higher-half). */
void mmu_enable_caches(void);

void mmu_ttbr0_install(uint64_t root_pa, uint16_t asid);
uint64_t mmu_ttbr0_create_minimal(void);

#ifdef __cplusplus
}
#endif
