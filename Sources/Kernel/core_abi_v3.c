#include "core_kernel_abi_v3.h"

#include "contracts.h"
#include "ipc/endpoint.h"
#include "kheap.h"
#include "sched/sched.h"
#include "sched/thread.h"
#include "task/task.h"
#include "uart_pl011.h"

#include "cap/cap_ops.h"
#include "cap/cap_status_ks.h"

// Reuse the "current task cap-space" convention from ABI v2.
static inline cap_table_t *current_caps(void) {
    thread_t *cur = sched_current();
    if (!cur || !cur->task) {
        return NULL;
    }
    return cur->task->caps;
}

static void ks_log(const char *s) {
    if (!s) return;
    uart_puts(s);
    uart_putc('\n');
}

static void *ks_alloc(size_t size) {
    ASSERT_THREAD_CONTEXT();
    return kmalloc(size);
}

static void ks_free(void *ptr) {
    ASSERT_THREAD_CONTEXT();
    kfree(ptr);
}

static void ks_yield(void) {
    ASSERT_THREAD_CONTEXT();
    yield();
}

// Capability ops (v2 semantics) exposed through v3.
static ks_cap_status_t ks_cap_dup_impl(ks_cap_handle_t h,
                                       ks_cap_rights_t mask,
                                       ks_cap_handle_t *out) {
    ASSERT_THREAD_CONTEXT();
    if (!out) return KS_CAP_ERR_INVALID;
    cap_table_t *t = current_caps();
    if (!t) return KS_CAP_ERR_INVALID;

    cap_handle_t new_h = 0;
    cap_status_t s = cap_dup(t, (cap_handle_t)h, t, (cap_rights_t)mask, &new_h);
    *out = (ks_cap_handle_t)new_h;
    return cap_status_to_ks_status(s);
}

static ks_cap_status_t ks_cap_transfer_impl(ks_cap_handle_t h,
                                            ks_cap_rights_t mask,
                                            ks_cap_handle_t *out) {
    ASSERT_THREAD_CONTEXT();
    if (!out) return KS_CAP_ERR_INVALID;
    cap_table_t *t = current_caps();
    if (!t) return KS_CAP_ERR_INVALID;

    cap_handle_t new_h = 0;
    cap_status_t s = cap_transfer(t, (cap_handle_t)h, t, (cap_rights_t)mask, &new_h);
    *out = (ks_cap_handle_t)new_h;
    return cap_status_to_ks_status(s);
}

static ks_cap_status_t ks_cap_drop_impl(ks_cap_handle_t h) {
    ASSERT_THREAD_CONTEXT();
    cap_table_t *t = current_caps();
    if (!t) return KS_CAP_ERR_INVALID;
    return cap_status_to_ks_status(cap_drop(t, (cap_handle_t)h));
}

static ks_cap_status_t ks_cap_invalidate_impl(ks_cap_handle_t h) {
    ASSERT_THREAD_CONTEXT();
    cap_table_t *t = current_caps();
    if (!t) return KS_CAP_ERR_INVALID;
    return cap_status_to_ks_status(cap_invalidate(t, (cap_handle_t)h));
}

// IPC (v3)
static ks_ipc_status_t ks_endpoint_create_impl(ks_cap_rights_t rights, ks_cap_handle_t *out) {
    ASSERT_THREAD_CONTEXT();
    if (!out) return KS_IPC_ERR_INVALID;
    cap_table_t *t = current_caps();
    if (!t) return KS_IPC_ERR_INVALID;

    cap_handle_t h = 0;
    ks_ipc_status_t st = endpoint_create_cap(t, (cap_rights_t)rights, &h);
    *out = (ks_cap_handle_t)h;
    return st;
}

static ks_ipc_status_t ks_ipc_send_impl(ks_cap_handle_t endpoint, const ks_ipc_msg_t *msg) {
    ASSERT_THREAD_CONTEXT();
    cap_table_t *t = current_caps();
    if (!t) return KS_IPC_ERR_INVALID;
    return ipc_send_cap(t, (cap_handle_t)endpoint, msg);
}

static ks_ipc_status_t ks_ipc_recv_impl(ks_cap_handle_t endpoint, ks_ipc_msg_t *out) {
    ASSERT_THREAD_CONTEXT();
    cap_table_t *t = current_caps();
    if (!t) return KS_IPC_ERR_INVALID;
    return ipc_recv_cap(t, (cap_handle_t)endpoint, out);
}

// v3 extends v2; keep the v2 prefix stable.
static const kernel_services_v3_t g_kernel_services_v3 = {
    .abi_version = 3,
    .reserved0   = 0,
    .log         = ks_log,
    .alloc       = ks_alloc,
    .free        = ks_free,
    .yield       = ks_yield,

    .cap_dup        = ks_cap_dup_impl,
    .cap_transfer   = ks_cap_transfer_impl,
    .cap_drop       = ks_cap_drop_impl,
    .cap_invalidate = ks_cap_invalidate_impl,

    .endpoint_create = ks_endpoint_create_impl,
    .ipc_send        = ks_ipc_send_impl,
    .ipc_recv        = ks_ipc_recv_impl,
};

const kernel_services_v3_t *kernel_services_v3(void) {
    return &g_kernel_services_v3;
}
