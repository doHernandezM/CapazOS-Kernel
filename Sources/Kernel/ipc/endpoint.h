#pragma once

#include <stdint.h>

typedef struct endpoint {
    uint64_t id;
    // Future: queues, rights checks, wait lists, etc.
} endpoint_t;

static inline void endpoint_init(endpoint_t *e, uint64_t id) {
    e->id = id;
}
