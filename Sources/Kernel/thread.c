// Kernel/Sources/Kernel/thread.c
// Cooperative thread creation + bootstrap.

#include "thread.h"

#include <stddef.h>
#include <stdint.h>

#include "kheap.h"
#include "mem.h"
#include "panic.h"
#include "pmm.h"
#include "sched.h"

// Preemption-ready threads resume via an IRQ-return trap frame (M8 Option A).
#include "irq.h"

// AArch64 SPSR value for returning to EL1h with IRQs enabled.
//
// LK reference/inspiration:
// LK uses an EL1h SPSR template with *all* DAIF bits masked:
//   ((0b1111u << 6) | 0b0101u)  // 0x3c5
// (see lk-master/arch/arm64/asm.S).
//
// For CapazOS threads we want IRQs enabled (I=0) but we still keep D/A/F masked
// (D=1, A=1, F=1) to match the "minimal surprises" early-kernel posture.
//
// DAIF bits are [9:6] = D A I F, mode bits [3:0] = 0b0101 for EL1h.
// So: DAIF = 0b1101 (D=1,A=1,I=0,F=1), M = 0b0101 => (0xD << 6) | 0x5 = 0x345.
#define SPSR_EL1H_IRQ_ENABLED 0x00000345u

// Provided by Sources/Arch/aarch64/context_switch.S.
extern void thread_start(void);

static uint32_t s_next_tid = 1;



static inline uint64_t align_down_u64(uint64_t v, uint64_t a) {
    return v & ~(a - 1u);
}

__attribute__((noreturn)) void thread_trampoline(void (*entry)(void *), void *arg) {
    entry(arg);
    thread_exit();
}

// Build an initial IRQ-return frame at the top of the new thread's kernel stack.
//
// The saved frame pointer (t->irq_sp) is later used by M8 to "return from IRQ into
// a different thread" by switching SP to this trap frame before restoring and ERET.
static uintptr_t thread_build_initial_irq_frame(thread_t *t, void (*entry)(void *), void *arg,
                                               uintptr_t stack_top_aligned) {
    // Reserve an initial trap frame at the top of the kernel stack.
    // This frame is consumed if/when the scheduler starts the thread via an
    // IRQ-return (eret) path. Until then, cooperative startup uses an SP below
    // this reserved area so normal stack usage does not clobber it.
    uintptr_t tf_sp = align_down_u64(stack_top_aligned - (uintptr_t)sizeof(trap_frame_t), 16);
    trap_frame_t *tf = (trap_frame_t *)tf_sp;
    memset(tf, 0, sizeof(*tf));

    // On first entry via IRQ-return, restore places x0/x1 and eret to ELR.
    // Make ELR point at the C trampoline so it can call entry(arg) then exit.
    tf->x[0] = (uint64_t)(uintptr_t)entry;
    tf->x[1] = (uint64_t)(uintptr_t)arg;
    // Phase 3 (M8): first-run/resume uses an IRQ-return frame (eret-based).
    // Seed ELR/SPSR so the thread starts at thread_trampoline(entry, arg) with
    // IRQs enabled in the restored PSTATE.
    tf->elr_el1  = (uint64_t)(uintptr_t)thread_trampoline;
    tf->spsr_el1 = (uint64_t)SPSR_EL1H_IRQ_ENABLED;

    // This field is used for diagnostics; on a real IRQ it records the interrupted
    // SP before the trap frame was allocated. For a new thread, make it look like
    // the thread was "interrupted" at the normal stack top.
    tf->sp_at_fault = (uint64_t)stack_top_aligned;

    // Record the "resume from" frame pointer for M8. This is the stack pointer
    // the IRQ exit path can switch to before restoring regs and eret.
    t->irq_sp    = (uint64_t)tf_sp;
    t->last_trap = tf;

    return (uintptr_t)tf_sp;
}

thread_t *thread_create(void (*entry)(void *), void *arg) {
    return thread_create_named(NULL, entry, arg);
}

thread_t *thread_create_named(const char *name, void (*entry)(void *), void *arg) {
    if (!entry) {
        panic("thread_create: entry is NULL");
    }

    // Allocate the thread object.
    thread_t *t = (thread_t *)kmalloc(sizeof(thread_t));
    if (!t) {
        panic("thread_create: OOM thread_t");
    }
    memset(t, 0, sizeof(*t));

    t->tid = s_next_tid++;
    t->name = name;

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

    // Build the initial IRQ-return frame at the top of the stack.
    // This also sets t->irq_sp to the frame pointer.
    const uintptr_t tf_sp = thread_build_initial_irq_frame(t, entry, arg, (uintptr_t)sp);

    // Initialize cooperative context. ctx_switch restores x19-x30 + sp.
    // We pass entry/arg through callee-saved regs so we do not depend on x0/x1.
    t->ctx.x19 = (uint64_t)(uintptr_t)entry;
    t->ctx.x20 = (uint64_t)(uintptr_t)arg;
    t->ctx.x30 = (uint64_t)(uintptr_t)&thread_start;
    // Start the cooperative stack pointer below the reserved initial trap frame
    // so normal stack usage does not clobber it before first preemption.
    // (Keep 16-byte alignment for AAPCS64.)
    t->ctx.sp  = align_down_u64((uint64_t)tf_sp, 16u) - 0x100;

    t->state = THREAD_READY;
    t->saved_daif = 0; // default: IRQs unmasked
    return t;
}

__attribute__((noreturn)) void thread_exit(void) {
    // Cooperative exit: mark dead and yield to another runnable thread.
    thread_t *t = sched_current();
    if (t) {
        t->state = THREAD_DEAD;
    }

    for (;;) {
        yield();
        // If nothing else is runnable, yield() returns immediately.
        __asm__ volatile("wfi");
    }
}
