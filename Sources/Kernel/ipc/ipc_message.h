#pragma once
#include <stddef.h>
#include <stdint.h>

#include "alloc/slab_cache.h"

typedef struct ipc_msg {
    struct ipc_msg *next;
    struct ipc_msg *prev;
    void *payload;
    size_t len;
    uint32_t tag;
} ipc_msg_t;

/* M5.5: slab-backed cache for IPC message objects (high churn). */
void ipc_msg_cache_init(void);
ipc_msg_t *ipc_msg_alloc(void);
void ipc_msg_free(ipc_msg_t *m);

/* Observability (M5.5 Phase 3). Returns false if cache not initialized. */
bool ipc_msg_cache_get_stats(slab_cache_stats_t *out);
