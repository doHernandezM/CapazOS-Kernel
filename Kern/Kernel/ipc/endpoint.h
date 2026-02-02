#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "api/ks_types.h"   // ks_ipc_msg_t, ks_ipc_status_t
#include "api/ks_status.h"  // ks_status_t

#include "cap/cap_table.h"        // cap_table_t, cap_handle_t, cap_rights_t

// Forward declarations to avoid pulling sched headers into all users.
typedef struct thread thread_t;
typedef struct endpoint endpoint_t;

// Allocate and initialize a raw endpoint object (no capability created).
// Used by early bootstrap code.
ks_status_t endpoint_create(endpoint_t **out_ep);


struct endpoint {
    uint64_t id;

    // Message queue (doubly-linked list of ipc_msg_t)
    struct ipc_msg *q_head;
    struct ipc_msg *q_tail;

    // Single waiting receiver: minimal blocking primitive.
    thread_t *waiting_recv;

    bool closed;
};

// Slab-backed endpoint objects (dynamic allocation).
void endpoint_cache_init(void);
endpoint_t *endpoint_alloc(void);
void endpoint_free(endpoint_t *e);

// Capability-scoped endpoint operations.
// These are kernel mechanisms; policy remains in Core.
ks_ipc_status_t endpoint_create_cap(cap_table_t *caps,
                                   cap_rights_t rights,
                                   cap_handle_t *out);

ks_ipc_status_t ipc_send_cap(cap_table_t *caps,
                             cap_handle_t endpoint_h,
                             const ks_ipc_msg_t *msg);

ks_ipc_status_t ipc_recv_cap(cap_table_t *caps,
                             cap_handle_t endpoint_h,
                             ks_ipc_msg_t *out);

// Non-blocking receive: returns KS_IPC_ERR_EMPTY if the queue is empty.
ks_ipc_status_t ipc_try_recv_cap(cap_table_t *caps,
                                 cap_handle_t endpoint_h,
                                 ks_ipc_msg_t *out);
