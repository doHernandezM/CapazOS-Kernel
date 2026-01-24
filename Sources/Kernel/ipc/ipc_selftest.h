#pragma once

#include "task/task.h"

// Debug-only IPC selftest (M8 readiness).
// Runs entirely in thread context and panics on failure.
void ipc_selftest(task_t *task);
