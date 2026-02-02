#include "task.h"

void task_init(task_t *t, cap_table_t *caps)
{
    if (!t) return;
    t->id = 0;
    t->caps = caps;
    t->self_cap = CAP_HANDLE_INVALID;
    t->console_ep_cap = CAP_HANDLE_INVALID;
}
