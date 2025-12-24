//
//  mmu_task_space.h
//  Capaz
//
//  Created by Cosas on 12/23/25.
//

#pragma once
#include <stdint.h>

#include "vm_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

// Minimal representation of a per-task TTBR0 space.
// Phase 3.0: placeholder only; later chunks will give this real ownership/ASIDs.
typedef struct mmu_task_space {
    uint64_t ttbr0_pa;   // L1 base PA for TTBR0 (0 means “use current” in stubs)
    uint16_t asid;       // reserved for Phase 3.1+
    uint16_t flags;      // reserved
    uint32_t reserved;
} mmu_task_space_t;

// Phase 3.0: stubs acceptable
mmu_task_space_t* mmu_task_space_create(void);
void mmu_task_space_activate(const mmu_task_space_t* space);

// Test hook: no faults required in 3.0. Return 1 if “mapped”, else 0.
int mmu_probe_user_va(const mmu_task_space_t* space, uint64_t user_va);

#ifdef __cplusplus
}
#endif
