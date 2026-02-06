#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "cap/cap_table.h"
#include "sched/intent.h"

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

    // Task-wide scheduler policy and ceilings.
    sched_intent_t default_intent;

    uint64_t limit_window_ticks;
    uint64_t cpu_ticks_limit;
    uint64_t io_read_bytes_limit;
    uint64_t io_write_bytes_limit;
    uint64_t mem_bytes_limit;

    // Window-local accounting used for enforcement.
    uint64_t window_start_tick;
    uint64_t cpu_ticks_window;
    uint64_t io_read_bytes_window;
    uint64_t io_write_bytes_window;

    // Lifetime counters (observability).
    uint64_t cpu_ticks_total;
    uint64_t io_read_bytes_total;
    uint64_t io_write_bytes_total;
    uint64_t mem_bytes_current;
    uint64_t mem_bytes_peak;
    uint64_t io_ops_total;
    uint64_t io_ops_background;
    uint64_t io_ops_latency;
    uint64_t io_ops_throughput;
    uint64_t io_ops_interactive;

    uint64_t cpu_throttle_events;
    uint64_t io_throttle_events;
    uint64_t mem_throttle_events;
} task_t;

typedef struct task_contract {
    uint32_t intent;
    uint32_t flags;
    uint64_t window_ticks;
    uint64_t cpu_ticks_limit;
    uint64_t io_read_bytes_limit;
    uint64_t io_write_bytes_limit;
    uint64_t mem_bytes_limit;
} task_contract_t;

typedef struct task_stats {
    uint64_t tick_now;
    uint64_t window_start_tick;
    uint64_t window_ticks;

    uint64_t cpu_ticks_total;
    uint64_t cpu_ticks_window;
    uint64_t cpu_throttle_events;

    uint64_t io_read_bytes_total;
    uint64_t io_read_bytes_window;
    uint64_t io_write_bytes_total;
    uint64_t io_write_bytes_window;
    uint64_t io_throttle_events;

    uint64_t mem_bytes_current;
    uint64_t mem_bytes_peak;
    uint64_t mem_throttle_events;

    uint64_t io_ops_total;
    uint64_t io_ops_interactive;
    uint64_t io_ops_latency;
    uint64_t io_ops_throughput;
    uint64_t io_ops_background;
} task_stats_t;

void task_init(task_t *t, cap_table_t *caps);
void task_set_contract(task_t *t, const task_contract_t *contract, uint64_t now_tick);
void task_get_contract(const task_t *t, task_contract_t *out_contract);
void task_get_stats(const task_t *t, uint64_t now_tick, task_stats_t *out_stats);

void task_account_cpu_tick(task_t *t, uint64_t now_tick);
bool task_allow_io(task_t *t, uint64_t bytes, bool is_write, uint64_t now_tick);
void task_account_io(task_t *t, uint64_t bytes, bool is_write, uint32_t intent, uint64_t now_tick);
bool task_allow_mem_alloc(task_t *t, uint64_t bytes);
void task_account_mem_alloc(task_t *t, uint64_t bytes);
void task_account_mem_free(task_t *t, uint64_t bytes);
