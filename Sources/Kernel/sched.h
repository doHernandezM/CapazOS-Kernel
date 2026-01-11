// Kernel/Sources/Kernel/sched.h
#pragma once

// Set to 1 to enable extra scheduler integrity checks.
#ifndef SCHED_DEBUG
#define SCHED_DEBUG 0
#endif


#include "thread.h"

// Initialize scheduler state for the currently running context (kmain/bootstrap thread).
void sched_init_bootstrap(void);

// Add a thread to the ready queue.
void sched_enqueue(thread_t *t);

// Cooperative yield: switch to the next runnable thread (if any).
void yield(void);

/*
 * Called from the IRQ exception path just before restoring the trap frame.
 * For now this is a no-op hook that records the current thread's last trap
 * frame pointer and re-validates scheduler invariants while IRQs are masked.
 *
 * M8 will use this hook to perform IRQ-return reschedules by returning a
 * different trap frame pointer.
 */
trap_frame_t *sched_irq_exit(trap_frame_t *tf);

// Return the currently running thread (bootstrap thread included).
thread_t *sched_current(void);
