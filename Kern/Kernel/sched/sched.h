// OS/Kern/Kernel/sched.h
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

// Block the currently running thread and reschedule.
// Thread context only. The thread remains blocked until woken via sched_wake().
void sched_block_current(void);

// Wake a blocked thread (moves it to ready queue).
void sched_wake(thread_t *t);

/*
 * Called from the IRQ exception path just before restoring the trap frame.
 *
 * Today this hook records the current thread's most recent trap frame pointer
 * and re-validates scheduler invariants while IRQs are masked.
 *
 * A future preemptive scheduler may use this hook to return a different trap
 * frame pointer in order to resume a different thread on IRQ return.
 */
trap_frame_t *sched_irq_exit(trap_frame_t *tf);

// Return the currently running thread (bootstrap thread included).
thread_t *sched_current(void);
