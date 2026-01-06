// Kernel/Sources/Kernel/sched.c
//
// M7 Phase 5: minimal cooperative round-robin scheduler.
//
// Design constraints (locked in for M8 preemption):
//  - Cooperative only: threads switch only when they explicitly call yield().
//  - Per-thread kernel stacks: ctx_switch swaps SP.
//  - Callee-saved context: ctx_switch saves/restores x19-x30 + SP.
//
// Run queue model:
//  - `current` is *not* in the ready queue while running.
//  - ready queue is a circular singly-linked list, tracked by a tail pointer.
//    If `ready` is non-NULL, the head is `ready->rq_next`.

#include "sched.h"

#include <stddef.h>

#include "irq.h"
#include "panic.h"

// Bootstrap pseudo-thread for the initial kernel context (kmain).
static thread_t bootstrap_thread;

static thread_t *current;
static thread_t *ready; // tail pointer (see file header)

// Internal helper: pop the head of the circular ready list.
static thread_t *dequeue_head(void) {
    if (!ready) {
        return NULL;
    }

    thread_t *head = ready->rq_next;
    if (head == ready) {
        // Single element.
        ready = NULL;
    } else {
        // Remove head by linking tail to head->next.
        ready->rq_next = head->rq_next;
    }
    head->rq_next = NULL;
    return head;
}

void sched_init_bootstrap(void) {
    // Minimal init: represent the current execution context as a pseudo-thread.
    // ctx is filled on the first ctx_switch out of this context.
    bootstrap_thread.rq_next = NULL;
    bootstrap_thread.last_trap = NULL;
    bootstrap_thread.saved_daif = 0;
    bootstrap_thread.state = THREAD_RUNNING;

    current = &bootstrap_thread;
    ready = NULL;
}

void sched_enqueue(thread_t *t) {
    if (!t) {
        panic("sched_enqueue: t is NULL");
    }
    if (t->state != THREAD_READY) {
        panic("sched_enqueue: thread not READY");
    }
    if (t->rq_next) {
        panic("sched_enqueue: thread already queued");
    }

    if (!ready) {
        // First element: self-linked single-node ring.
        ready = t;
        t->rq_next = t;
        return;
    }

    // Insert at tail in O(1):
    //   head = ready->next
    //   ready->next = t
    //   t->next = head
    //   ready = t
    thread_t *head = ready->rq_next;
    ready->rq_next = t;
    t->rq_next = head;
    ready = t;
}

// Pick the next runnable thread.
// Returns NULL if there is no other runnable thread.
static thread_t *sched_pick_next(void) {
    return dequeue_head();
}

void yield(void) {
    if (!current) {
        panic("yield: scheduler not initialized");
    }

    // Mask IRQs while we manipulate scheduler state and switch stacks.
    uint64_t daif = irq_save();

    // If there is nobody else ready, just restore IRQ state and continue.
    if (!ready) {
        irq_restore(daif);
        return;
    }

    thread_t *prev = current;
    prev->saved_daif = daif;

    // Re-queue the current thread (unless it's the bootstrap context or DEAD).
    if (prev != &bootstrap_thread && prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        sched_enqueue(prev);
    }

    thread_t *next = sched_pick_next();
    if (!next) {
        // Shouldn't happen because we checked !ready above, but be defensive.
        irq_restore(daif);
        return;
    }

    next->state = THREAD_RUNNING;
    current = next;

    // Switch context. Note: when switching to a brand-new thread, execution
    // will begin at thread_start (Arch/aarch64/context_switch.S) instead of
    // returning here immediately.
    ctx_switch(&prev->ctx, &next->ctx);

    // When we resume here later, we are running on *this thread's* stack.
    // Restore the IRQ mask state that was active when this thread last yielded.
    irq_restore(current->saved_daif);
}

// Private accessor used by thread_exit() (not part of the public scheduler API).
thread_t *sched_get_current(void) {
    return current;
}
