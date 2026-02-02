#pragma once

#include <stdint.h>

#include "cap/cap_table.h"

// A task represents an execution context with its own capability space.
// During bring-up Core runs in the kernel task context.

typedef struct task {
    uint64_t id;
    cap_table_t *caps;

    // Capabilities seeded at bootstrap.
    cap_handle_t self_cap;
    cap_handle_t console_ep_cap; // endpoint capability for early console/logging
} task_t;

void task_init(task_t *t, cap_table_t *caps);
