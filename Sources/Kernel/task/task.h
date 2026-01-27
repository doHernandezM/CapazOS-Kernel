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
    cap_table_t *caps; // capability space owned by this task (Phase 2)

    // Bootstrap handles seeded for the initial kernel task (Phase 2).
    cap_handle_t self_cap;
    cap_handle_t timer_cap;
    cap_handle_t log_cap;
} task_t;

static inline void task_init(task_t *t, uint64_t id, cap_table_t *caps) {
    t->id = id;
    t->caps = caps;
    t->self_cap = 0;
    t->timer_cap = 0;
    t->log_cap = 0;
}
