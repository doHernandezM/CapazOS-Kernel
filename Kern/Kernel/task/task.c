#include "task.h"

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
}
