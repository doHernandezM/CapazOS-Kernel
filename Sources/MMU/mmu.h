#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Phase 2 compatibility (now delegates to Phase 3 bootstrap).
void mmu_early_enable(void);
void mmu_kernel_init_global(void);
void mmu_adopt_boot_pt_allocator(void);
// Phase 3.0/3.1 interfaces.
void mmu_bootstrap(void);

void mmu_enable_caches(void);

void mmu_ttbr0_install(uint64_t root_pa, uint16_t asid);
uint64_t mmu_ttbr0_create_minimal(void);

#ifdef __cplusplus
}
#endif
