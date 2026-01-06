// Kernel/Sources/Kernel/thread.c
// Cooperative thread creation + bootstrap.

#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#include "kheap.h"
#include "panic.h"
#include "pmm.h"
#include "sched.h"

// Provided by Sources/Arch/aarch64/context_switch.S.
extern void thread_start(void);

// Defined in Sources/Kernel/mem.c (freestanding libc shim).
void *memset(void *dst, int c, size_t n);

static inline uint64_t align_down_u64(uint64_t v, uint64_t a) {
    return v & ~(a - 1u);
}

__attribute__((noreturn)) void thread_trampoline(void (*entry)(void *), void *arg) {
    entry(arg);
    thread_exit();
}

thread_t *thread_create(void (*entry)(void *), void *arg) {
    if (!entry) {
        panic("thread_create: entry is NULL");
    }

    // Allocate the thread object.
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    if (!t) {
        panic("thread_create: OOM thread_t");
    }
    memset(t, 0, sizeof(*t));

    // Allocate a per-thread kernel stack from PMM pages.
    // Phase 0 default: 16 KiB (4 pages). This remains a per-thread contract.
    const uint32_t pages = (uint32_t)KSTACK_PAGES_DEFAULT;
    uint64_t stack_pa = 0;
    if (!pmm_alloc_pages(pages, &stack_pa)) {
        kfree(t);
        panic("thread_create: OOM stack pages");
    }

    void *stack_va = (void *)pmm_phys_to_virt(stack_pa);
    const size_t stack_size = (size_t)pages * (size_t)KSTACK_PAGE_SIZE;
    void *stack_top = (void *)((uintptr_t)stack_va + stack_size);

    // 16-byte align the initial SP (AAPCS64).
    uint64_t sp = align_down_u64((uint64_t)(uintptr_t)stack_top, 16u);

    t->kstack_base = stack_va;
    t->kstack_size = stack_size;
    t->kstack_top  = (void *)(uintptr_t)sp;

    // Initialize cooperative context. ctx_switch restores x19-x30 + sp.
    // We pass entry/arg through callee-saved regs so we do not depend on x0/x1.
    t->ctx.x19 = (uint64_t)(uintptr_t)entry;
    t->ctx.x20 = (uint64_t)(uintptr_t)arg;
    t->ctx.x30 = (uint64_t)(uintptr_t)&thread_start;
    t->ctx.sp  = sp;

    t->state = THREAD_READY;
    return t;
}

__attribute__((noreturn)) void thread_exit(void) {
    // Mark this thread dead and cooperatively yield to another runnable thread.
    // If no other threads are runnable, yield() returns and we park the CPU.
    extern thread_t *sched_get_current(void);
    thread_t *self = sched_get_current();
    if (self) {
        self->state = THREAD_DEAD;
    }

    for (;;) {
        yield();
        __asm__ volatile("wfi");
    }
}
