#include "task.h"

#include <stddef.h>

static inline uint64_t saturating_sub_u64(uint64_t a, uint64_t b)
{
    return (a > b) ? (a - b) : 0;
}

static void task_roll_window(task_t *t, uint64_t now_tick)
{
    if (!t) {
        return;
    }
    const uint64_t window = (t->limit_window_ticks == 0) ? 1 : t->limit_window_ticks;
    if (now_tick < t->window_start_tick) {
        t->window_start_tick = now_tick;
        t->cpu_ticks_window = 0;
        t->io_read_bytes_window = 0;
        t->io_write_bytes_window = 0;
        return;
    }
    if ((now_tick - t->window_start_tick) < window) {
        return;
    }

    uint64_t spans = (now_tick - t->window_start_tick) / window;
    if (spans == 0) {
        spans = 1;
    }
    t->window_start_tick += spans * window;
    t->cpu_ticks_window = 0;
    t->io_read_bytes_window = 0;
    t->io_write_bytes_window = 0;
}

void task_init(task_t *t, cap_table_t *caps)
{
    if (!t) return;
    t->id = 0;
    t->caps = caps;
    t->self_cap = CAP_HANDLE_INVALID;
    t->console_ep_cap = CAP_HANDLE_INVALID;
    t->uart_cmd_ep_cap = CAP_HANDLE_INVALID;
    t->uart_evt_ep_cap = CAP_HANDLE_INVALID;
    t->kernel_log_send_cap = CAP_HANDLE_INVALID;
    t->kernel_log_recv_cap = CAP_HANDLE_INVALID;

    t->default_intent = SCHED_INTENT_INTERACTIVE;

    t->limit_window_ticks = 100;
    t->cpu_ticks_limit = 0;
    t->io_read_bytes_limit = 0;
    t->io_write_bytes_limit = 0;
    t->mem_bytes_limit = 0;

    t->window_start_tick = 0;
    t->cpu_ticks_window = 0;
    t->io_read_bytes_window = 0;
    t->io_write_bytes_window = 0;

    t->cpu_ticks_total = 0;
    t->io_read_bytes_total = 0;
    t->io_write_bytes_total = 0;
    t->mem_bytes_current = 0;
    t->mem_bytes_peak = 0;
    t->io_ops_total = 0;
    t->io_ops_background = 0;
    t->io_ops_latency = 0;
    t->io_ops_throughput = 0;
    t->io_ops_interactive = 0;

    t->cpu_throttle_events = 0;
    t->io_throttle_events = 0;
    t->mem_throttle_events = 0;
}

void task_set_contract(task_t *t, const task_contract_t *contract, uint64_t now_tick)
{
    if (!t || !contract) {
        return;
    }

    t->default_intent = sched_intent_sanitize(contract->intent);
    t->limit_window_ticks = (contract->window_ticks == 0) ? 100 : contract->window_ticks;
    t->cpu_ticks_limit = contract->cpu_ticks_limit;
    t->io_read_bytes_limit = contract->io_read_bytes_limit;
    t->io_write_bytes_limit = contract->io_write_bytes_limit;
    t->mem_bytes_limit = contract->mem_bytes_limit;

    t->window_start_tick = now_tick;
    t->cpu_ticks_window = 0;
    t->io_read_bytes_window = 0;
    t->io_write_bytes_window = 0;
}

void task_get_contract(const task_t *t, task_contract_t *out_contract)
{
    if (!t || !out_contract) {
        return;
    }
    out_contract->intent = (uint32_t)t->default_intent;
    out_contract->flags = 0;
    out_contract->window_ticks = t->limit_window_ticks;
    out_contract->cpu_ticks_limit = t->cpu_ticks_limit;
    out_contract->io_read_bytes_limit = t->io_read_bytes_limit;
    out_contract->io_write_bytes_limit = t->io_write_bytes_limit;
    out_contract->mem_bytes_limit = t->mem_bytes_limit;
}

void task_get_stats(const task_t *t, uint64_t now_tick, task_stats_t *out_stats)
{
    if (!t || !out_stats) {
        return;
    }

    out_stats->tick_now = now_tick;
    out_stats->window_start_tick = t->window_start_tick;
    out_stats->window_ticks = t->limit_window_ticks;

    out_stats->cpu_ticks_total = t->cpu_ticks_total;
    out_stats->cpu_ticks_window = t->cpu_ticks_window;
    out_stats->cpu_throttle_events = t->cpu_throttle_events;

    out_stats->io_read_bytes_total = t->io_read_bytes_total;
    out_stats->io_read_bytes_window = t->io_read_bytes_window;
    out_stats->io_write_bytes_total = t->io_write_bytes_total;
    out_stats->io_write_bytes_window = t->io_write_bytes_window;
    out_stats->io_throttle_events = t->io_throttle_events;

    out_stats->mem_bytes_current = t->mem_bytes_current;
    out_stats->mem_bytes_peak = t->mem_bytes_peak;
    out_stats->mem_throttle_events = t->mem_throttle_events;

    out_stats->io_ops_total = t->io_ops_total;
    out_stats->io_ops_interactive = t->io_ops_interactive;
    out_stats->io_ops_latency = t->io_ops_latency;
    out_stats->io_ops_throughput = t->io_ops_throughput;
    out_stats->io_ops_background = t->io_ops_background;
}

void task_account_cpu_tick(task_t *t, uint64_t now_tick)
{
    if (!t) {
        return;
    }
    task_roll_window(t, now_tick);
    t->cpu_ticks_total++;
    t->cpu_ticks_window++;
}

bool task_allow_io(task_t *t, uint64_t bytes, bool is_write, uint64_t now_tick)
{
    if (!t) {
        return true;
    }
    task_roll_window(t, now_tick);

    if (is_write) {
        if (t->io_write_bytes_limit != 0 &&
            (t->io_write_bytes_window + bytes) > t->io_write_bytes_limit) {
            t->io_throttle_events++;
            return false;
        }
    } else {
        if (t->io_read_bytes_limit != 0 &&
            (t->io_read_bytes_window + bytes) > t->io_read_bytes_limit) {
            t->io_throttle_events++;
            return false;
        }
    }
    return true;
}

void task_account_io(task_t *t, uint64_t bytes, bool is_write, uint32_t intent, uint64_t now_tick)
{
    if (!t) {
        return;
    }
    task_roll_window(t, now_tick);

    if (is_write) {
        t->io_write_bytes_total += bytes;
        t->io_write_bytes_window += bytes;
    } else {
        t->io_read_bytes_total += bytes;
        t->io_read_bytes_window += bytes;
    }
    t->io_ops_total++;

    switch (sched_intent_sanitize(intent)) {
        case SCHED_INTENT_INTERACTIVE:
            t->io_ops_interactive++;
            break;
        case SCHED_INTENT_LATENCY:
            t->io_ops_latency++;
            break;
        case SCHED_INTENT_THROUGHPUT:
            t->io_ops_throughput++;
            break;
        case SCHED_INTENT_BACKGROUND:
        default:
            t->io_ops_background++;
            break;
    }
}

bool task_allow_mem_alloc(task_t *t, uint64_t bytes)
{
    if (!t) {
        return true;
    }
    if (t->mem_bytes_limit == 0) {
        return true;
    }
    if ((t->mem_bytes_current + bytes) > t->mem_bytes_limit) {
        t->mem_throttle_events++;
        return false;
    }
    return true;
}

void task_account_mem_alloc(task_t *t, uint64_t bytes)
{
    if (!t) {
        return;
    }
    t->mem_bytes_current += bytes;
    if (t->mem_bytes_current > t->mem_bytes_peak) {
        t->mem_bytes_peak = t->mem_bytes_current;
    }
}

void task_account_mem_free(task_t *t, uint64_t bytes)
{
    if (!t) {
        return;
    }
    t->mem_bytes_current = saturating_sub_u64(t->mem_bytes_current, bytes);
}
