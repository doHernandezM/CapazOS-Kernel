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
    cap_handle_t console_ep_cap;   // endpoint capability for early console/logging
    cap_handle_t uart_cmd_ep_cap;  // UART driver command endpoint
    cap_handle_t uart_evt_ep_cap;  // UART driver event endpoint
    cap_handle_t kernel_log_send_cap; // KernelLog send endpoint
    cap_handle_t kernel_log_recv_cap; // KernelLog recv endpoint (seeded to Core)
} task_t;

void task_init(task_t *t, cap_table_t *caps);
