//
//  vm_layout.h
//  Capaz
//
//  Phase 3: define a kernel-vs-user virtual address layout.
//  - Kernel runs in the high half (translated by TTBR1).
//  - User/task VA lives in the low half (translated by TTBR0).
//

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// QEMU virt loads the kernel image physically at 0x4000_0000 by convention.
#define KERNEL_PA_BASE        0x40000000ull

// 39-bit VA canonical high region base is 0xFFFFFF8000000000.
// For Option A (higher-half), choose a VA base that keeps a constant offset to PA.
// NOTE: No underscores in C integer literals.
#define KERNEL_VA_BASE        0xFFFFFF8040000000ull
#define KERNEL_VA_OFFSET      (KERNEL_VA_BASE - KERNEL_PA_BASE)

// Phase 3.0 API contract: user VA window (TTBR0).
#define USER_VA_BASE          0x0000000000400000ull
#define USER_VA_LIMIT         0x0000000040000000ull  // exclusive

// Future kernel MMIO window (TTBR1 plan).
#define KERNEL_MMIO_BASE      0xFFFFFF9000000000ull

// Optional: keep a small unmapped "null guard" region in user space.
#define USER_NULL_GUARD_PAGES 1ull

// Helpers used by stubs/tests (must exist for mmu_task_space.c).
static inline int vm_va_is_user(uint64_t va) {
    return (va >= USER_VA_BASE) && (va < USER_VA_LIMIT);
}
static inline int vm_va_is_kernel(uint64_t va) {
    return !vm_va_is_user(va);
}

// Translate helpers for Option A higher-half mapping.
static inline uint64_t kva_to_pa(uint64_t va) { return (uint64_t)(va - KERNEL_VA_OFFSET); }
static inline uint64_t pa_to_kva(uint64_t pa) { return (uint64_t)(pa + KERNEL_VA_OFFSET); }

// ---- User VA window (TTBR0) ----
// Keep user VA strictly below the kernel image's low VA alias (0x4000_0000)
// to avoid overlap during transitional bring-up.
#define USER_VA_BASE   0x0000000000400000ull
#define USER_VA_LIMIT  0x0000000040000000ull  /* exclusive */

// Backwards-compatible aliases (older code may refer to MIN/MAX).
#define USER_VA_MIN    USER_VA_BASE
#define USER_VA_MAX    (USER_VA_LIMIT - 1ull)

// Optional convention: keep a small unmapped "null guard" at the bottom of user VA.
#define USER_NULL_GUARD_PAGES 1ull



// QEMU virt PL011 UART physical base.
#define UART0_PA_BASE      0x0000000009000000ull

// Kernel VA mapping for UART0 (must be mapped in TTBR1).
// We place it at (KERNEL_MMIO_BASE + UART0_PA_BASE) to keep it deterministic.
#define KERNEL_MMIO_UART0_BASE (KERNEL_MMIO_BASE + UART0_PA_BASE)


#ifdef __cplusplus
}
#endif

