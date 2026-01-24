#include "ipc/endpoint.h"

#include <stddef.h>

#include "alloc/slab_cache.h"
#include "contracts.h"
#include "debug/panic.h"
#include "irq.h"
#include "mm/mem.h"          // memset, memcpy
#include "ipc/ipc_message.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "cap/cap_ops.h"
#include "cap/cap_entry.h"
#include "cap/cap_rights.h"
#include "cap/cap_types.h"

static slab_cache_t g_endpoint_cache;
static bool s_endpoint_cache_inited = false;
static uint64_t s_next_endpoint_id = 1;

void endpoint_cache_init(void) {
    if (s_endpoint_cache_inited) return;
    slab_cache_init(&g_endpoint_cache, "endpoint", sizeof(endpoint_t), (size_t)_Alignof(endpoint_t));
    s_endpoint_cache_inited = true;
}

endpoint_t *endpoint_alloc(void) {
    ASSERT_THREAD_CONTEXT();
    if (!s_endpoint_cache_inited) {
        panic("endpoint_alloc: cache not initialized");
    }
    endpoint_t *e = (endpoint_t *)slab_alloc(&g_endpoint_cache);
    if (!e) {
        panic("endpoint_alloc: OOM");
    }
    memset(e, 0, sizeof(*e));
    e->id = s_next_endpoint_id++;
    return e;
}

void endpoint_free(endpoint_t *e) {
    ASSERT_THREAD_CONTEXT();
    if (!e) return;
    if (!s_endpoint_cache_inited) {
        panic("endpoint_free: cache not initialized");
    }
    slab_free(&g_endpoint_cache, e);
}

static inline void q_push_tail(endpoint_t *e, ipc_msg_t *m) {
    m->next = NULL;
    m->prev = e->q_tail;
    if (e->q_tail) {
        e->q_tail->next = m;
    } else {
        e->q_head = m;
    }
    e->q_tail = m;
}

static inline ipc_msg_t *q_pop_head(endpoint_t *e) {
    ipc_msg_t *m = e->q_head;
    if (!m) return NULL;
    ipc_msg_t *n = m->next;
    e->q_head = n;
    if (n) {
        n->prev = NULL;
    } else {
        e->q_tail = NULL;
    }
    m->next = NULL;
    m->prev = NULL;
    return m;
}

static inline endpoint_t *endpoint_from_handle(cap_table_t *caps,
                                               cap_handle_t h,
                                               cap_rights_t need_rights,
                                               ks_ipc_status_t *out_status) {
    if (!caps) {
        if (out_status) *out_status = KS_IPC_ERR_INVALID;
        return NULL;
    }
    cap_entry_t *ent = cap_table_lookup(caps, h, need_rights);
    if (!ent) {
        if (out_status) *out_status = KS_IPC_ERR_RIGHTS;
        return NULL;
    }
    if (ent->type != CAP_TYPE_ENDPOINT || !ent->obj) {
        if (out_status) *out_status = KS_IPC_ERR_INVALID;
        return NULL;
    }
    if (out_status) *out_status = KS_IPC_OK;
    return (endpoint_t *)ent->obj;
}

ks_ipc_status_t endpoint_create_cap(cap_table_t *caps,
                                   cap_rights_t rights,
                                   cap_handle_t *out) {
    ASSERT_THREAD_CONTEXT();
    if (!caps || !out) {
        return KS_IPC_ERR_INVALID;
    }

    // Ensure endpoint cache exists.
    if (!s_endpoint_cache_inited) {
        endpoint_cache_init();
    }

    endpoint_t *e = endpoint_alloc();
    if (!e) {
        return KS_IPC_ERR_NO_MEM;
    }

    // Ensure callers can always drop what they create.
    cap_rights_t eff = rights | CAP_R_DROP;
    cap_handle_t h = 0;
    cap_status_t st = cap_create(caps, CAP_TYPE_ENDPOINT, eff, (void *)e, &h);
    if (st != CAP_OK) {
        endpoint_free(e);
        return (st == CAP_ERR_NO_MEM) ? KS_IPC_ERR_NO_MEM : KS_IPC_ERR_INVALID;
    }

    *out = h;
    return KS_IPC_OK;
}

ks_ipc_status_t ipc_send_cap(cap_table_t *caps,
                             cap_handle_t endpoint_h,
                             const ks_ipc_msg_t *msg) {
    ASSERT_THREAD_CONTEXT();
    if (!msg) {
        return KS_IPC_ERR_INVALID;
    }
    if (msg->len > KS_IPC_MSG_MAX) {
        return KS_IPC_ERR_INVALID;
    }

    ks_ipc_status_t status = KS_IPC_OK;
    endpoint_t *e = endpoint_from_handle(caps, endpoint_h, CAP_R_SEND, &status);
    if (!e) return status;

    if (e->closed) {
        return KS_IPC_ERR_CLOSED;
    }

    // Allocate a kernel-owned message object and copy inline payload.
    ipc_msg_t *m = ipc_msg_alloc();
    if (!m) {
        return KS_IPC_ERR_NO_MEM;
    }
    m->tag = msg->tag;
    m->len = msg->len;
    if (m->len > 0) {
        memcpy(m->data, msg->data, m->len);
    }

    // Enqueue under IRQ mask.
    uint64_t flags = irq_save();
    q_push_tail(e, m);

    // Wake a waiting receiver if present.
    thread_t *w = e->waiting_recv;
    if (w) {
        e->waiting_recv = NULL;
        // Wake the blocked receiver.
        sched_wake(w);
    }
    irq_restore(flags);
    return KS_IPC_OK;
}

ks_ipc_status_t ipc_recv_cap(cap_table_t *caps,
                             cap_handle_t endpoint_h,
                             ks_ipc_msg_t *out) {
    ASSERT_THREAD_CONTEXT();
    if (!out) {
        return KS_IPC_ERR_INVALID;
    }

    ks_ipc_status_t status = KS_IPC_OK;
    endpoint_t *e = endpoint_from_handle(caps, endpoint_h, CAP_R_RECV, &status);
    if (!e) return status;

    for (;;) {
        uint64_t flags = irq_save();
        ipc_msg_t *m = q_pop_head(e);
        if (m) {
            irq_restore(flags);
            // Copy out and free.
            out->tag = m->tag;
            out->len = m->len;
            if (out->len > KS_IPC_MSG_MAX) {
                // Should never happen; clamp defensively.
                out->len = KS_IPC_MSG_MAX;
            }
            if (out->len > 0) {
                memcpy(out->data, m->data, out->len);
            }
            ipc_msg_free(m);
            return KS_IPC_OK;
        }

        if (e->closed) {
            irq_restore(flags);
            return KS_IPC_ERR_CLOSED;
        }

        // Queue empty: block until a sender wakes us.
        thread_t *cur = sched_current();
        if (!cur) {
            irq_restore(flags);
            return KS_IPC_ERR_INVALID;
        }
        // Only one waiter supported in this minimal design.
        if (e->waiting_recv && e->waiting_recv != cur) {
            irq_restore(flags);
            return KS_IPC_ERR_RIGHTS;
        }
        e->waiting_recv = cur;
        irq_restore(flags);

        // Block and reschedule. When we resume, loop and try again.
        sched_block_current();
    }
}
