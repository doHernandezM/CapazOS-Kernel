// Kernel/Sources/Kernel/sched.c
//
// M7 Phase 5/6: Minimal cooperative round-robin scheduler.
//
// Design:
//  - current thread is NOT in the ready queue while running.
//  - ready queue is a circular singly-linked list tracked by a tail pointer.
//  - threads switch only when they explicitly call yield().

#include "sched.h"

#include <stddef.h>

#include "panic.h"

// For trap frame sizing checks (irq_sp range validation).
#include "irq.h"
#include "context.h"
#include "preempt.h"

#define SCHED_ASSERT(cond, msg) do { if (!(cond)) panic(msg); } while (0)

// Provided by Sources/Arch/aarch64/context_switch.S.
extern void ctx_switch(ctx_t *old, ctx_t *new);

// Bootstrap pseudo-thread (CPU0 initial context).
static thread_t bootstrap_thread;

static thread_t *s_current = NULL;
// Tail pointer for circular list. Head is s_ready_tail->rq_next.
static thread_t *s_ready_tail = NULL;

// sched.c
static thread_t bootstrap_thread;

static inline void sched_validate_irq_sp(thread_t *t) {
    if (!t) return;
    if (t == &bootstrap_thread) return; // bootstrap has no per-thread stack
    SCHED_ASSERT(t->kstack_base != NULL, "sched: thread kstack_base is NULL");
    SCHED_ASSERT(t->kstack_top != NULL, "sched: thread kstack_top is NULL");
    SCHED_ASSERT(t->kstack_size != 0,   "sched: thread kstack_size is 0");
    SCHED_ASSERT(t->irq_sp != 0, "sched: thread irq_sp is NULL");
    SCHED_ASSERT((t->irq_sp & 0xF) == 0, "sched: thread irq_sp not 16-byte aligned");

    uintptr_t base = (uintptr_t)t->kstack_base;
    uintptr_t top  = (uintptr_t)t->kstack_top;
    uintptr_t sp   = (uintptr_t)t->irq_sp;

    // irq_sp points at a trap frame living *within* the kernel stack.
    SCHED_ASSERT(sp >= base, "sched: thread irq_sp below stack base");
    SCHED_ASSERT((sp + sizeof(trap_frame_t)) <= top, "sched: thread irq_sp beyond stack top");
}
static inline void rq_validate(void);

static inline void rq_insert_tail(thread_t *t) {
    if (!t) return;
    if (t->rq_next) {
        panic("sched: enqueue of already-queued thread");
    }
    SCHED_ASSERT(t->state == THREAD_READY, "sched: enqueue requires THREAD_READY");

    // Preemption path will require a valid irq_sp for any non-bootstrap runnable thread.
    sched_validate_irq_sp(t);
    if (t->ctx.sp) {
        SCHED_ASSERT((t->ctx.sp & 0xF) == 0, "sched: thread ctx.sp not 16-byte aligned");
    }

    if (!s_ready_tail) {
        t->rq_next = t;
        s_ready_tail = t;
        return;
    }

    t->rq_next = s_ready_tail->rq_next;
    s_ready_tail->rq_next = t;
    s_ready_tail = t;
}

static inline uint64_t rq_critical_enter(void) {
    uint64_t flags = irq_save();
    preempt_disable();
    return flags;
}

static inline void rq_critical_exit(uint64_t flags) {
    preempt_enable();
    irq_restore(flags);
}

static inline thread_t *rq_pop_head(void) {
    uint64_t flags = rq_critical_enter();
    if (!s_ready_tail) {
        rq_critical_exit(flags);
        return NULL;
    }

    thread_t *head = s_ready_tail->rq_next;
    if (head == s_ready_tail) {
        // Single element.
        s_ready_tail = NULL;
    } else {
        s_ready_tail->rq_next = head->rq_next;
    }
    head->rq_next = NULL;
    rq_validate();

    rq_critical_exit(flags);
    return head;
}

static inline void rq_validate(void) {
#if SCHED_DEBUG
    if (!s_ready_tail) return;
    thread_t *head = s_ready_tail->rq_next;
    SCHED_ASSERT(head != NULL, "sched: ready head is NULL");
    // Walk at most 1024 nodes to catch corruption without hanging.
    thread_t *t = head;
    for (unsigned i = 0; i < 1024; i++) {
        SCHED_ASSERT(t != NULL, "sched: ready node is NULL");
        SCHED_ASSERT(t->rq_next != NULL, "sched: ready node rq_next NULL");
        if (t->rq_next == head) return; // closed circle
        t = t->rq_next;
    }
    panic("sched: ready queue corrupted (no cycle closure)");
#endif
}

thread_t *sched_current(void) {
    return s_current;
}

void sched_init_bootstrap(void) {
    bootstrap_thread.ctx = (ctx_t){0};
    bootstrap_thread.kstack_base = NULL;
    bootstrap_thread.kstack_size = 0;
    bootstrap_thread.kstack_top  = NULL;
    bootstrap_thread.rq_next     = NULL;
    bootstrap_thread.last_trap   = NULL;
    bootstrap_thread.saved_daif  = 0;
    bootstrap_thread.state       = THREAD_RUNNING;

    s_current = &bootstrap_thread;
}

void sched_enqueue(thread_t *t) {
    if (!t) return;

    uint64_t flags = rq_critical_enter();

    // If a thread is DEAD, it must not be re-enqueued.
    if (t->state == THREAD_DEAD) {
        rq_critical_exit(flags);
        return;
    }

    // Only READY threads belong on the ready queue.
    t->state = THREAD_READY;
    rq_insert_tail(t);
    rq_validate();

    rq_critical_exit(flags);
}

static thread_t *sched_pick_next(void) {
    thread_t *next = rq_pop_head();
    if (!next) {
        return s_current;
    }
    return next;
}

void yield(void) {
    
    ASSERT_THREAD_CONTEXT();
uint64_t flags = irq_save();
    thread_t *prev = s_current;
    SCHED_ASSERT(prev != NULL, "sched: current is NULL");
    SCHED_ASSERT(prev->rq_next == NULL, "sched: current unexpectedly enqueued");

    // Only enqueue non-bootstrap runnable threads
    if (prev != &bootstrap_thread && prev->state != THREAD_DEAD) {
        prev->state = THREAD_READY;
        sched_enqueue(prev);
    }

    thread_t *next = sched_pick_next();
    if (!next) {
        irq_restore(flags);
        return;
    }

    next->state = THREAD_RUNNING;
    if (next != &bootstrap_thread) {
        SCHED_ASSERT(next->ctx.sp != 0, "sched: next thread has NULL ctx.sp");
    }
    s_current = next;
    ctx_switch(&prev->ctx, &next->ctx);

    irq_restore(flags);
}

trap_frame_t *sched_irq_exit(trap_frame_t *tf) {
    /*
     * Phase 0: preemption-readiness audit.
     *
     * Contract:
     *  - IRQs remain masked across irq_dispatch() and this hook.
     *  - The IRQ exit path restores register state from whatever SP points at
     *    and then ERET's. M8 will switch threads by returning a different
     *    trap frame pointer here.
     */
    SCHED_ASSERT(irq_irqs_disabled(), "sched: IRQs must be masked in sched_irq_exit");
    SCHED_ASSERT(tf != NULL, "sched: NULL trap frame");

    thread_t *cur = s_current;
    // The timer/IRQ subsystem can be brought up before cooperative threads.
    // In that early-boot window there is no "current" thread to tag; just
    // preserve the IRQ return frame unchanged.
    if (cur == NULL) {
        return tf;
    }
    SCHED_ASSERT(cur->rq_next == NULL, "sched: current unexpectedly enqueued in irq exit");

    /*
     * Always keep a pointer to the most recent trap for debugging/introspection.
     *
     * IMPORTANT: cur->irq_sp is only meaningful when a thread is *switched away*
     * at IRQ exit (its saved trap frame remains resident on its own stack).
     * If we return to the same thread, the IRQ exit path restores from tf and
     * then pops the frame; persisting tf into cur->irq_sp would leave a stale
     * resume pointer.
     */
    cur->last_trap = tf;

    /* Queue integrity checks are safe here because IRQs are still masked. */
    rq_validate();

    return tf;
}

