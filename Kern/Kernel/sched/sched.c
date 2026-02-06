// OS/Kern/Kernel/sched.c
//
// Intent-aware single-CPU scheduler.

#include "sched.h"

#include <stddef.h>

#include "context.h"
#include "irq.h"
#include "panic.h"
#include "preempt.h"
#include "config.h"
#include "task/task.h"

#define SCHED_ASSERT(cond, msg) do { if (!(cond)) panic(msg); } while (0)

// Provided by Sources/Arch/aarch64/context_switch.S.
extern void ctx_switch(ctx_t *old, ctx_t *new);

// Bootstrap pseudo-thread (CPU0 initial context).
static thread_t bootstrap_thread;

static thread_t *s_current = NULL;
// Per-intent circular ready queues tracked by tail pointers.
static thread_t *s_ready_tail[SCHED_INTENT_COUNT];

static uint64_t s_sched_ticks = 0;
static sched_stats_t s_stats;

static const sched_intent_t k_pick_pattern[] = {
    SCHED_INTENT_INTERACTIVE,
    SCHED_INTENT_INTERACTIVE,
    SCHED_INTENT_INTERACTIVE,
    SCHED_INTENT_INTERACTIVE,
    SCHED_INTENT_INTERACTIVE,
    SCHED_INTENT_INTERACTIVE,
    SCHED_INTENT_LATENCY,
    SCHED_INTENT_LATENCY,
    SCHED_INTENT_LATENCY,
    SCHED_INTENT_LATENCY,
    SCHED_INTENT_THROUGHPUT,
    SCHED_INTENT_THROUGHPUT,
    SCHED_INTENT_BACKGROUND,
};
static uint32_t s_pick_cursor = 0;

static inline uint64_t sched_slice_for_intent(sched_intent_t intent)
{
    switch (intent) {
        case SCHED_INTENT_INTERACTIVE:
            return 2;
        case SCHED_INTENT_LATENCY:
            return 3;
        case SCHED_INTENT_THROUGHPUT:
            return 5;
        case SCHED_INTENT_BACKGROUND:
        default:
            return 8;
    }
}

static inline void sched_validate_irq_sp(thread_t *t)
{
    if (!t) return;
    if (t == &bootstrap_thread) return; // bootstrap has no per-thread stack

    SCHED_ASSERT(t->kstack_base != NULL, "sched: thread kstack_base is NULL");
    SCHED_ASSERT(t->kstack_top != NULL, "sched: thread kstack_top is NULL");
    SCHED_ASSERT(t->kstack_size != 0, "sched: thread kstack_size is 0");
    SCHED_ASSERT(t->irq_sp != 0, "sched: thread irq_sp is NULL");
    SCHED_ASSERT((t->irq_sp & 0xF) == 0, "sched: thread irq_sp not 16-byte aligned");

    uintptr_t base = (uintptr_t)t->kstack_base;
    uintptr_t top = (uintptr_t)t->kstack_top;
    uintptr_t sp = (uintptr_t)t->irq_sp;

    SCHED_ASSERT(sp >= base, "sched: thread irq_sp below stack base");
    SCHED_ASSERT((sp + sizeof(trap_frame_t)) <= top, "sched: thread irq_sp beyond stack top");
}

static inline bool rq_empty_intent(sched_intent_t intent)
{
    return s_ready_tail[intent] == NULL;
}

static inline void rq_insert_tail_locked(thread_t *t)
{
    if (!t) return;
    if (t->rq_next) {
        panic("sched: enqueue of already-queued thread");
    }
    SCHED_ASSERT(t->state == THREAD_READY, "sched: enqueue requires THREAD_READY");

    sched_validate_irq_sp(t);
    if (t->ctx.sp) {
        SCHED_ASSERT((t->ctx.sp & 0xF) == 0, "sched: thread ctx.sp not 16-byte aligned");
    }

    sched_intent_t intent = sched_intent_sanitize(t->sched_intent);
    thread_t *tail = s_ready_tail[intent];
    if (!tail) {
        t->rq_next = t;
        s_ready_tail[intent] = t;
        return;
    }

    t->rq_next = tail->rq_next;
    tail->rq_next = t;
    s_ready_tail[intent] = t;
}

static bool rq_remove_locked(thread_t *t)
{
    if (!t) return false;

    for (uint32_t i = 0; i < (uint32_t)SCHED_INTENT_COUNT; i++) {
        thread_t *tail = s_ready_tail[i];
        if (!tail) {
            continue;
        }

        thread_t *prev = tail;
        thread_t *cur = tail->rq_next;
        do {
            if (cur == t) {
                if (cur == prev) {
                    s_ready_tail[i] = NULL;
                } else {
                    prev->rq_next = cur->rq_next;
                    if (s_ready_tail[i] == cur) {
                        s_ready_tail[i] = prev;
                    }
                }
                cur->rq_next = NULL;
                return true;
            }
            prev = cur;
            cur = cur->rq_next;
        } while (prev != tail);
    }

    return false;
}

static thread_t *rq_pop_head_intent_locked(sched_intent_t intent)
{
    thread_t *tail = s_ready_tail[intent];
    if (!tail) {
        return NULL;
    }

    thread_t *head = tail->rq_next;
    if (head == tail) {
        s_ready_tail[intent] = NULL;
    } else {
        tail->rq_next = head->rq_next;
    }
    head->rq_next = NULL;
    return head;
}

static sched_intent_t sched_pick_intent_locked(void)
{
#if !CONFIG_SCHED_INTENT
    for (uint32_t i = 0; i < (uint32_t)SCHED_INTENT_COUNT; i++) {
        if (!rq_empty_intent((sched_intent_t)i)) {
            return (sched_intent_t)i;
        }
    }
    return SCHED_INTENT_BACKGROUND;
#else
    const uint32_t pattern_len = (uint32_t)(sizeof(k_pick_pattern) / sizeof(k_pick_pattern[0]));

    for (uint32_t i = 0; i < pattern_len; i++) {
        uint32_t idx = (s_pick_cursor + i) % pattern_len;
        sched_intent_t intent = k_pick_pattern[idx];
        if (!rq_empty_intent(intent)) {
            s_pick_cursor = (idx + 1) % pattern_len;
            return intent;
        }
    }

    for (uint32_t i = 0; i < (uint32_t)SCHED_INTENT_COUNT; i++) {
        if (!rq_empty_intent((sched_intent_t)i)) {
            return (sched_intent_t)i;
        }
    }

    return SCHED_INTENT_BACKGROUND;
#endif
}

static thread_t *rq_pop_best_locked(void)
{
    sched_intent_t intent = sched_pick_intent_locked();
    return rq_pop_head_intent_locked(intent);
}

static inline void sched_note_run_intent(sched_intent_t intent)
{
    switch (intent) {
        case SCHED_INTENT_INTERACTIVE:
            s_stats.intent_runs_interactive++;
            break;
        case SCHED_INTENT_LATENCY:
            s_stats.intent_runs_latency++;
            break;
        case SCHED_INTENT_THROUGHPUT:
            s_stats.intent_runs_throughput++;
            break;
        case SCHED_INTENT_BACKGROUND:
        default:
            s_stats.intent_runs_background++;
            break;
    }
}

static inline void sched_prepare_running_locked(thread_t *t)
{
    if (!t) {
        return;
    }
    sched_intent_t intent = sched_intent_sanitize(t->sched_intent);
    t->state = THREAD_RUNNING;
    t->slice_ticks_budget = sched_slice_for_intent(intent);
    t->slice_ticks_used = 0;
    sched_note_run_intent(intent);
}

static inline void rq_validate(void)
{
#if SCHED_DEBUG
    for (uint32_t i = 0; i < (uint32_t)SCHED_INTENT_COUNT; i++) {
        thread_t *tail = s_ready_tail[i];
        if (!tail) {
            continue;
        }

        thread_t *head = tail->rq_next;
        SCHED_ASSERT(head != NULL, "sched: ready head is NULL");

        thread_t *t = head;
        for (unsigned n = 0; n < 2048; n++) {
            SCHED_ASSERT(t != NULL, "sched: ready node is NULL");
            SCHED_ASSERT(t->rq_next != NULL, "sched: ready node rq_next NULL");
            if (t->rq_next == head) {
                break;
            }
            t = t->rq_next;
            if (n == 2047) {
                panic("sched: ready queue corrupted (no cycle closure)");
            }
        }
    }
#endif
}

thread_t *sched_current(void)
{
    return s_current;
}

uint64_t sched_ticks_now(void)
{
    return s_sched_ticks;
}

void sched_get_stats(sched_stats_t *out_stats)
{
    if (!out_stats) {
        return;
    }
    *out_stats = s_stats;
    out_stats->tick_now = s_sched_ticks;
}

void sched_init_bootstrap(void)
{
    for (uint32_t i = 0; i < (uint32_t)SCHED_INTENT_COUNT; i++) {
        s_ready_tail[i] = NULL;
    }

    bootstrap_thread.ctx = (ctx_t){0};
    bootstrap_thread.tid = 0;
    bootstrap_thread.name = "bootstrap";
    bootstrap_thread.task = NULL;
    bootstrap_thread.kstack_base = NULL;
    bootstrap_thread.kstack_size = 0;
    bootstrap_thread.kstack_top = NULL;
    bootstrap_thread.rq_next = NULL;
    bootstrap_thread.last_trap = NULL;
    bootstrap_thread.irq_sp = 0;
    bootstrap_thread.saved_daif = 0;
    bootstrap_thread.sched_intent = (uint8_t)SCHED_INTENT_INTERACTIVE;
    bootstrap_thread.slice_ticks_budget = 0;
    bootstrap_thread.slice_ticks_used = 0;
    bootstrap_thread.runtime_ticks_total = 0;
    bootstrap_thread.preemptions = 0;
    bootstrap_thread.state = THREAD_RUNNING;

    s_sched_ticks = 0;
    s_pick_cursor = 0;
    s_stats = (sched_stats_t){0};

    s_current = &bootstrap_thread;
}

void sched_set_thread_intent(thread_t *t, uint32_t intent_raw)
{
    if (!t) {
        return;
    }

    uint64_t flags = irq_save();
    sched_intent_t next_intent = sched_intent_sanitize(intent_raw);

    if ((sched_intent_t)t->sched_intent == next_intent) {
        irq_restore(flags);
        return;
    }

    bool was_ready = (t->state == THREAD_READY) && (t->rq_next != NULL);
    if (was_ready) {
        (void)rq_remove_locked(t);
    }

    t->sched_intent = (uint8_t)next_intent;
    if (was_ready) {
        rq_insert_tail_locked(t);
    }

    irq_restore(flags);
}

void sched_enqueue(thread_t *t)
{
    if (!t) return;

    uint64_t flags = irq_save();

    if (t->state == THREAD_DEAD) {
        irq_restore(flags);
        return;
    }

    t->state = THREAD_READY;
    rq_insert_tail_locked(t);
    rq_validate();

    irq_restore(flags);
}

static thread_t *sched_pick_next_locked(void)
{
    thread_t *next = rq_pop_best_locked();
    if (!next) {
        return s_current;
    }
    return next;
}

void yield(void)
{
    ASSERT_THREAD_CONTEXT();

    uint64_t flags = irq_save();
    s_stats.yield_calls++;

    thread_t *prev = s_current;
    SCHED_ASSERT(prev != NULL, "sched: current is NULL");
    SCHED_ASSERT(prev->rq_next == NULL, "sched: current unexpectedly enqueued");

    preempt_clear_need_resched();

    if (prev != &bootstrap_thread && prev->state == THREAD_RUNNING) {
        prev->state = THREAD_READY;
        rq_insert_tail_locked(prev);
    }

    thread_t *next = sched_pick_next_locked();
    if (!next) {
        irq_restore(flags);
        return;
    }

    if (next == prev) {
        sched_prepare_running_locked(prev);
        irq_restore(flags);
        return;
    }

    sched_prepare_running_locked(next);
    s_current = next;
    s_stats.context_switches++;
    ctx_switch(&prev->ctx, &next->ctx);

    irq_restore(flags);
}

void sched_block_current(void)
{
    ASSERT_THREAD_CONTEXT();

    uint64_t flags = irq_save();

    thread_t *prev = s_current;
    SCHED_ASSERT(prev != NULL, "sched: current is NULL");
    SCHED_ASSERT(prev->rq_next == NULL, "sched: current unexpectedly enqueued");
    SCHED_ASSERT(prev != &bootstrap_thread, "sched: bootstrap thread must not block");

    prev->state = THREAD_BLOCKED;

    thread_t *next = sched_pick_next_locked();
    if (!next || next == prev) {
        prev->state = THREAD_RUNNING;
        irq_restore(flags);
        return;
    }

    sched_prepare_running_locked(next);
    s_current = next;
    s_stats.context_switches++;
    ctx_switch(&prev->ctx, &next->ctx);

    irq_restore(flags);
}

void sched_wake(thread_t *t)
{
    ASSERT_THREAD_CONTEXT();
    if (!t) return;

    uint64_t flags = irq_save();

    if (t->state == THREAD_BLOCKED) {
        t->state = THREAD_READY;
        rq_insert_tail_locked(t);
    }

    irq_restore(flags);
}

void sched_on_timer_tick(void)
{
    s_sched_ticks++;

#if CONFIG_SCHED_COOPERATIVE || !CONFIG_PREEMPT
    return;
#else
    thread_t *cur = s_current;
    if (!cur) {
        preempt_set_need_resched();
        return;
    }

    if (cur != &bootstrap_thread) {
        cur->runtime_ticks_total++;

        task_t *task = cur->task;
        if (task) {
            task_account_cpu_tick(task, s_sched_ticks);
#if CONFIG_RES_CEILINGS
            if (task->cpu_ticks_limit != 0 && task->cpu_ticks_window > task->cpu_ticks_limit) {
                if (task->cpu_ticks_window == (task->cpu_ticks_limit + 1)) {
                    task->cpu_throttle_events++;
                }
                // Limit overruns are demoted to background scheduling pressure.
                cur->sched_intent = (uint8_t)SCHED_INTENT_BACKGROUND;
                preempt_set_need_resched();
            }
#endif
        }
    }

    cur->slice_ticks_used++;
    if (cur->slice_ticks_budget == 0 || cur->slice_ticks_used >= cur->slice_ticks_budget) {
        preempt_set_need_resched();
    }
#endif
}

trap_frame_t *sched_irq_exit(trap_frame_t *tf)
{
    SCHED_ASSERT(irq_irqs_disabled(), "sched: IRQs must be masked in sched_irq_exit");
    SCHED_ASSERT(tf != NULL, "sched: NULL trap frame");

    thread_t *cur = s_current;
    if (cur == NULL) {
        return tf;
    }
    SCHED_ASSERT(cur->rq_next == NULL, "sched: current unexpectedly enqueued in irq exit");

    cur->last_trap = tf;
    rq_validate();

#if CONFIG_SCHED_COOPERATIVE || !CONFIG_PREEMPT
    return tf;
#else
    if (!preempt_need_resched()) {
        return tf;
    }
    if (!preemptible()) {
        return tf;
    }
    preempt_clear_need_resched();

    if (cur != &bootstrap_thread && cur->state == THREAD_RUNNING) {
        cur->irq_sp = (uint64_t)(uintptr_t)tf;
        cur->state = THREAD_READY;
        rq_insert_tail_locked(cur);
    }

    thread_t *next = sched_pick_next_locked();
    if (!next || next == cur) {
        cur->state = THREAD_RUNNING;
        sched_prepare_running_locked(cur);
        return tf;
    }

    SCHED_ASSERT(next->irq_sp != 0, "sched: next thread irq_sp is NULL");
    sched_prepare_running_locked(next);
    s_current = next;

    if (cur != &bootstrap_thread) {
        cur->preemptions++;
    }
    s_stats.preempt_switches++;
    s_stats.context_switches++;

    return (trap_frame_t *)(uintptr_t)next->irq_sp;
#endif
}
