#pragma once

#include <stdint.h>

// Forward declare cap table.
typedef struct cap_table cap_table_t;
// cap_handle_t is the opaque handle type that will eventually cross the Core ABI.
// It is defined in cap_table.h. Keep a fallback typedef here so task.h can be
// included without pulling in cap_table.h.
#ifndef CAP_HANDLE_T_DEFINED
typedef uint64_t cap_handle_t;
#endif

typedef struct task {
    uint64_t id;
    cap_table_t *caps; // capability space owned by this task

    // Bootstrap handles seeded for the initial kernel task.
    cap_handle_t self_cap;
    // Console endpoint caps seeded for Core bring-up.
    // These will become the first “real” capabilities Core uses.
    cap_handle_t console_req_ep;
    cap_handle_t console_rsp_ep;
    cap_handle_t console_ctl_ep;
} task_t;

static inline void task_init(task_t *t, uint64_t id, cap_table_t *caps) {
    t->id = id;
    t->caps = caps;
    t->self_cap = 0;
    t->console_req_ep = 0;
    t->console_rsp_ep = 0;
    t->console_ctl_ep = 0;
}
