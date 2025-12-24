//
//  vm_layout.h
//  Capaz
//
//  Created by Cosas on 12/22/25.
//

#pragma once
#include <stdint.h>

/*
 Phase 3 (planning): split TTBR1 (kernel) vs TTBR0 (user).

 Phase 3.0 deliverable:
   - freeze names/limits for kernel-vs-user ranges
   - no behavior changes required yet
*/

// QEMU virt loads the kernel image physically at 0x4000_0000 by convention.
#define KERNEL_PA_BASE        0x40000000ull

// Phase 3.0: future higher-half base for kernel-global mappings (TTBR1).
// 39-bit VA canonical high region base is 0xFFFF_FF80_0000_0000.
#define KERNEL_VA_BASE        0xFFFFFF8000000000ull

// If you later choose a constant-offset higher-half mapping for the kernel image:
#define KERNEL_IMAGE_VA_BASE  (KERNEL_VA_BASE + KERNEL_PA_BASE)
#define KERNEL_IMAGE_VA_OFFSET (KERNEL_IMAGE_VA_BASE - KERNEL_PA_BASE)

// Phase 3.0: user VA window (TTBR0).
// Keep it strictly below the kernel load address to avoid overlap during transition.
#define USER_VA_BASE          0x0000000000400000ull
#define USER_VA_LIMIT         0x0000000040000000ull  // exclusive upper bound

// Phase 3.0: future kernel MMIO window base (TTBR1 plan).
#define KERNEL_MMIO_BASE      0xFFFFFF9000000000ull

static inline int vm_va_is_user(uint64_t va) {
    return (va >= USER_VA_BASE) && (va < USER_VA_LIMIT);
}

static inline int vm_va_is_kernel(uint64_t va) {
    return !vm_va_is_user(va);
}

/* Backwards-compatible names (pre-3.0 drafts) */
#define USER_VA_MIN   USER_VA_BASE
#define USER_VA_MAX   (USER_VA_LIMIT - 1ull)
#define KERNEL_VA_OFFSET  KERNEL_IMAGE_VA_OFFSET
