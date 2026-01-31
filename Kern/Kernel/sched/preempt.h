// OS/Kern/Kernel/preempt.h
//
// Preemption bookkeeping: intent + preemption-disable depth.
//
// Single-CPU today, but shaped like per-CPU state so SMP can drop in later.

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct preempt_cpu {
    // Set by IRQ context (e.g. timer tick). Checked at IRQ-exit safe points.
    volatile uint32_t need_resched;

    // Depth counter that answers "is it legal to preempt here?".
    // Any scheduler/run-queue mutation must occur with preemption disabled.
    uint32_t preempt_count;
} preempt_cpu_t;

// Returns CPU-local preemption state (CPU0 for now).
preempt_cpu_t *preempt_cpu(void);

// Preemption disable/enable depth.
void preempt_disable(void);
void preempt_enable(void);

// True if preemption is allowed at this point.
bool preemptible(void);

// Scheduling intent flag (set by IRQ handlers).
void preempt_set_need_resched(void);
void preempt_clear_need_resched(void);
bool preempt_need_resched(void);
