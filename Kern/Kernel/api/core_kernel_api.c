#include "api/core_kernel_api.h"

#include "uart_pl011.h"
#include "kheap.h"
#include "sched.h"
#include "sched/thread.h"
#include "task/task.h"
#include "cap/cap_ops.h"
#include "cap/cap_status_ks.h"
#include "ipc/endpoint.h"

static inline cap_table_t *current_caps(void) {
    thread_t *t = sched_current();
    if (!t || !t->task || !t->task->caps) {
        return NULL;
    }
    return t->task->caps;
}

// ---------------- Logging ----------------

void cka_log_write(const char *cstr) {
    if (!cstr) return;
    uart_puts(cstr);
}

// ---------------- Allocation ----------------

void *cka_malloc(size_t size) {
    return kmalloc(size);
}

void cka_free(void *ptr) {
    kfree(ptr);
}

// ---------------- Scheduler ----------------

void cka_yield(void) {
    yield();
}

// ---------------- Capability operations ----------------

ks_cap_status_t cka_cap_dup(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out) {
    cap_table_t *caps = current_caps();
    if (!caps) return KS_CAP_ERR_INVALID;
    cap_handle_t out_h = 0;
    cap_status_t st = cap_dup(caps, (cap_handle_t)h, caps, (cap_rights_t)mask, &out_h);
    if (out) *out = (ks_cap_handle_t)out_h;
    return cap_status_to_ks_status(st);
}

ks_cap_status_t cka_cap_transfer(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out) {
    cap_table_t *caps = current_caps();
    if (!caps) return KS_CAP_ERR_INVALID;
    cap_handle_t out_h = 0;
    cap_status_t st = cap_transfer(caps, (cap_handle_t)h, caps, (cap_rights_t)mask, &out_h);
    if (out) *out = (ks_cap_handle_t)out_h;
    return cap_status_to_ks_status(st);
}

ks_cap_status_t cka_cap_drop(ks_cap_handle_t h) {
    cap_table_t *caps = current_caps();
    if (!caps) return KS_CAP_ERR_INVALID;
    cap_status_t st = cap_drop(caps, (cap_handle_t)h);
    return cap_status_to_ks_status(st);
}

ks_cap_status_t cka_cap_invalidate(ks_cap_handle_t h) {
    cap_table_t *caps = current_caps();
    if (!caps) return KS_CAP_ERR_INVALID;
    cap_status_t st = cap_invalidate(caps, (cap_handle_t)h);
    return cap_status_to_ks_status(st);
}

// ---------------- IPC ----------------

ks_ipc_status_t cka_endpoint_create(ks_cap_rights_t rights, ks_cap_handle_t *out) {
    cap_table_t *caps = current_caps();
    if (!caps || !out) return KS_IPC_ERR_INVALID;
    cap_handle_t h = 0;
    ks_ipc_status_t st = endpoint_create_cap(caps, (cap_rights_t)rights, &h);
    *out = (ks_cap_handle_t)h;
    return st;
}

ks_ipc_status_t cka_ipc_send(ks_cap_handle_t endpoint, const ks_ipc_msg_t *msg) {
    cap_table_t *caps = current_caps();
    if (!caps) return KS_IPC_ERR_INVALID;
    return ipc_send_cap(caps, (cap_handle_t)endpoint, msg);
}

ks_ipc_status_t cka_ipc_recv(ks_cap_handle_t endpoint, ks_ipc_msg_t *out) {
    cap_table_t *caps = current_caps();
    if (!caps) return KS_IPC_ERR_INVALID;
    return ipc_recv_cap(caps, (cap_handle_t)endpoint, out);
}
