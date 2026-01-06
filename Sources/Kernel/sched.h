// Kernel/Sources/Kernel/sched.h
#pragma once

#include "thread.h"

// Initialize scheduler state for the currently running context (kmain/bootstrap thread).
void sched_init_bootstrap(void);

// Add a thread to the ready queue.
void sched_enqueue(thread_t *t);

// Cooperative yield: switch to the next runnable thread (if any).
void yield(void);
