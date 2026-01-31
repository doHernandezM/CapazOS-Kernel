// OS/Kern/Kernel/preempt.c
//
// Preemption bookkeeping: intent + preemption-disable depth.

#include "preempt.h"

#include "panic.h"

// CPU0-only for now.
static preempt_cpu_t s_cpu0 = {
    .need_resched = 0,
    .preempt_count = 0,
};

preempt_cpu_t *preempt_cpu(void) {
    return &s_cpu0;
}

void preempt_disable(void) {
    preempt_cpu()->preempt_count++;
}

void preempt_enable(void) {
    if (preempt_cpu()->preempt_count == 0) {
        panic("preempt: enable with preempt_count == 0");
    }
    preempt_cpu()->preempt_count--;
}

bool preemptible(void) {
    return preempt_cpu()->preempt_count == 0;
}

void preempt_set_need_resched(void) {
    preempt_cpu()->need_resched = 1;
}

void preempt_clear_need_resched(void) {
    preempt_cpu()->need_resched = 0;
}

bool preempt_need_resched(void) {
    return preempt_cpu()->need_resched != 0;
}
