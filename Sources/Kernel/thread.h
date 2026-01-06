// Kernel/Sources/Kernel/thread.h
#pragma once

#include <stdint.h>
#include <stddef.h>

// Forward declaration (defined in irq.h).
typedef struct trap_frame trap_frame_t;

// Default kernel stack sizing for cooperative threads.
// Phase 0 decision: 16 KiB default, up to 64 KiB max.
#ifndef KSTACK_PAGES_DEFAULT
#define KSTACK_PAGES_DEFAULT 4u
#endif

#ifndef KSTACK_PAGES_MAX
#define KSTACK_PAGES_MAX 16u
#endif

#define KSTACK_PAGE_SIZE 4096u
#define KSTACK_SIZE_DEFAULT (KSTACK_PAGES_DEFAULT * KSTACK_PAGE_SIZE)
#define KSTACK_SIZE_MAX     (KSTACK_PAGES_MAX * KSTACK_PAGE_SIZE)

// Callee-saved context for cooperative switching.
// Layout is an ABI contract with Arch/aarch64/context_switch.S.
typedef struct ctx {
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
    uint64_t x29; // FP
    uint64_t x30; // LR
    uint64_t sp;
} ctx_t;

// ABI offset checks (must match assembly expectations).
#define CTX_OFF_X19 ((size_t)offsetof(ctx_t, x19))
#define CTX_OFF_X20 ((size_t)offsetof(ctx_t, x20))
#define CTX_OFF_X21 ((size_t)offsetof(ctx_t, x21))
#define CTX_OFF_X22 ((size_t)offsetof(ctx_t, x22))
#define CTX_OFF_X23 ((size_t)offsetof(ctx_t, x23))
#define CTX_OFF_X24 ((size_t)offsetof(ctx_t, x24))
#define CTX_OFF_X25 ((size_t)offsetof(ctx_t, x25))
#define CTX_OFF_X26 ((size_t)offsetof(ctx_t, x26))
#define CTX_OFF_X27 ((size_t)offsetof(ctx_t, x27))
#define CTX_OFF_X28 ((size_t)offsetof(ctx_t, x28))
#define CTX_OFF_X29 ((size_t)offsetof(ctx_t, x29))
#define CTX_OFF_X30 ((size_t)offsetof(ctx_t, x30))
#define CTX_OFF_SP  ((size_t)offsetof(ctx_t, sp))

_Static_assert(CTX_OFF_X19 == 0,   "ctx_t ABI: x19 offset");
_Static_assert(CTX_OFF_X20 == 8,   "ctx_t ABI: x20 offset");
_Static_assert(CTX_OFF_X21 == 16,  "ctx_t ABI: x21 offset");
_Static_assert(CTX_OFF_X22 == 24,  "ctx_t ABI: x22 offset");
_Static_assert(CTX_OFF_X23 == 32,  "ctx_t ABI: x23 offset");
_Static_assert(CTX_OFF_X24 == 40,  "ctx_t ABI: x24 offset");
_Static_assert(CTX_OFF_X25 == 48,  "ctx_t ABI: x25 offset");
_Static_assert(CTX_OFF_X26 == 56,  "ctx_t ABI: x26 offset");
_Static_assert(CTX_OFF_X27 == 64,  "ctx_t ABI: x27 offset");
_Static_assert(CTX_OFF_X28 == 72,  "ctx_t ABI: x28 offset");
_Static_assert(CTX_OFF_X29 == 80,  "ctx_t ABI: x29 offset");
_Static_assert(CTX_OFF_X30 == 88,  "ctx_t ABI: x30 offset");
_Static_assert(CTX_OFF_SP  == 96,  "ctx_t ABI: sp offset");
_Static_assert(sizeof(ctx_t) == 104, "ctx_t ABI: size");

typedef enum thread_state {
    THREAD_READY = 0,
    THREAD_RUNNING,
    THREAD_DEAD,
} thread_state_t;

typedef struct thread {
    ctx_t ctx;

    // Per-thread kernel stack.
    void   *kstack_base;
    size_t  kstack_size;
    void   *kstack_top;

    // Minimal run-queue linkage (circular singly-linked list).
    struct thread *rq_next;

    // Reserved for M8 preemption integration (stack on IRQ return).
    trap_frame_t *last_trap;

    // Saved interrupt mask state for cooperative scheduling.
    // yield() stores the prior DAIF here so it can be restored when the
    // thread resumes from ctx_switch.
    uint64_t saved_daif;

    thread_state_t state;
} thread_t;

// Assembly primitive.
void ctx_switch(ctx_t *old, ctx_t *new);

// Thread API (implemented starting in Phase 4).
thread_t *thread_create(void (*entry)(void *), void *arg);
__attribute__((noreturn)) void thread_trampoline(void (*entry)(void *), void *arg);
__attribute__((noreturn)) void thread_exit(void);
