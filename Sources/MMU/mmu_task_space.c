//
//  mmu_task_space.c
//  Capaz
//
//  Created by Cosas on 12/23/25.
//

#include "mmu_task_space.h"

// Phase 3.0: single placeholder space.
static mmu_task_space_t g_boot_space = {
    .ttbr0_pa = 0,
    .asid = 0,
    .flags = 0,
    .reserved = 0,
};

mmu_task_space_t* mmu_task_space_create(void) {
    return &g_boot_space;
}

void mmu_task_space_activate(const mmu_task_space_t* space) {
    (void)space;
    // Phase 3.0: no TTBR0 switching yet.
}

int mmu_probe_user_va(const mmu_task_space_t* space, uint64_t user_va) {
    (void)space;
    // Phase 3.0: no page table walk, no AT instruction yet.
    // Contract: user VA is “not accessible unless explicitly mapped”.
    if (!vm_va_is_user(user_va)) return 0;
    return 0;
}
