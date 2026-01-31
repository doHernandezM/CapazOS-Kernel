#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "alloc/slab_cache.h"

// Bring-up policy: inline payload only.
// Larger payloads will be supported later via a MEMOBJ capability.
#ifndef IPC_MSG_INLINE_MAX
#define IPC_MSG_INLINE_MAX 128u
#endif

typedef struct ipc_msg {
    struct ipc_msg *next;
    struct ipc_msg *prev;

    uint32_t tag;
    uint32_t len; // bytes valid in data[]
    uint8_t  data[IPC_MSG_INLINE_MAX];
} ipc_msg_t;

/* Slab-backed cache for IPC message objects (high churn). */
void ipc_msg_cache_init(void);
ipc_msg_t *ipc_msg_alloc(void);
void ipc_msg_free(ipc_msg_t *m);

/* Observability. Returns false if cache not initialized. */
bool ipc_msg_cache_get_stats(slab_cache_stats_t *out);
