#pragma once

// Kernel Services ABI v1
// Canonical contract of services the C Kernel provides to Core.
//
// Boundary rules:
// - POD types only.
// - No Swift types or opaque runtime types cross this boundary.
// - Function pointers must be stable across platforms/architectures.

#include <stddef.h>
#include <stdint.h>

// Versioning: bump MAJOR on breaking changes; bump MINOR on additive changes.
// v1.1 expands the table to include basic time + IRQ primitives.
#define CAPAZ_KERNEL_SERVICES_V1_MAJOR 1
#define CAPAZ_KERNEL_SERVICES_V1_MINOR 1

// Basic services.
typedef void (*kernel_logf_fn_t)(const char *fmt, ...);
typedef void (*kernel_panic_fn_t)(const char *msg) __attribute__((noreturn));

// Time.
typedef uint64_t (*kernel_time_now_ticks_fn_t)(void);

// IRQ primitives.
// irq_save returns an architecture-specific flags value that must be passed back to irq_restore.
typedef uint64_t (*kernel_irq_save_fn_t)(void);
typedef void (*kernel_irq_restore_fn_t)(uint64_t flags);

// Scheduling.
typedef void (*kernel_yield_fn_t)(void);

// Memory.
typedef void *(*kernel_alloc_fn_t)(size_t size, size_t align);
typedef void (*kernel_free_fn_t)(void *ptr);

// Services table layout. This struct must remain POD and stable.
typedef struct kernel_services_v1 {
    // ABI major version the kernel implements for this table.
    // (For v1.x this should be CAPAZ_KERNEL_SERVICES_V1_MAJOR.)
    uint32_t abi_version;

    // Logging and fatal errors.
    kernel_logf_fn_t log;
    kernel_panic_fn_t panic;

    // Memory management.
    kernel_alloc_fn_t alloc;
    kernel_free_fn_t free;

    // IRQ + time primitives (for Core critical sections / profiling).
    kernel_irq_save_fn_t irq_save;
    kernel_irq_restore_fn_t irq_restore;
    kernel_time_now_ticks_fn_t time_now_ticks;

    // Cooperative scheduling hook.
    kernel_yield_fn_t yield;
} kernel_services_v1_t;
