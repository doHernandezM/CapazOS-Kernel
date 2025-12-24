//
//  mmu.h
//  OSpost
//
//  Created by Cosas on 12/21/25.
//
#pragma once
#include <stdint.h>

#include "vm_layout.h"

/* Forward decl to keep mmu.h usable from early bring-up code. */
struct mmu_task_space;
typedef struct mmu_task_space mmu_task_space_t;

#ifdef __cplusplus
extern "C" {
#endif

// Phase 2 bring-up (current behavior)
void mmu_early_enable(void);
void mmu_enable_caches(void);

// --- Phase 3.0: address-space contract (stubs acceptable) ---

// Kernel-global mappings init (TTBR1 plan). Phase 3.0: wrapper around existing init.
void mmu_kernel_init_global(void);

// TTBR0 plan (per-task). Phase 3.0: stubs acceptable.
mmu_task_space_t* mmu_task_space_create(void);
void mmu_task_space_activate(const mmu_task_space_t* space);

// Test hook; no faults required in 3.0.
int mmu_probe_user_va(const mmu_task_space_t* space, uint64_t user_va);

#ifdef __cplusplus
}
#endif
