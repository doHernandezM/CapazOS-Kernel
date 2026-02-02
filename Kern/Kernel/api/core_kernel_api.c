#include "core_kernel_api.h"

#include "kheap.h"
#include "cap/cap_ops.h"
#include "mm/mem.h"
#include "sched/sched.h"
#include "serial/uart.h"

static cap_table_t *g_core_caps;

void cka_attach_core_caps(cap_table_t *core_caps)
{
    g_core_caps = core_caps;
}

cap_table_t *cka_core_caps(void)
{
    return g_core_caps;
}

// ---- Logging ----

void cka_early_log(const char *s)
{
    uart_puts(s);
}

void cka_console_write(const char *s, size_t len)
{
    // During early bring-up the console is UART-only.
    // Later this can forward to a console server endpoint.
    uart_write(s, len);
}

void cka_log_write(const char *s)
{
    if (!s) {
        return;
    }
    cka_console_write(s, strlen(s));
}

static ks_status_t ks_from_ipc_status(ks_ipc_status_t st)
{
    switch (st) {
        case KS_IPC_OK:          return KS_STATUS_OK;
        case KS_IPC_ERR_INVALID: return KS_STATUS_INVALID_ARG;
        case KS_IPC_ERR_RIGHTS:  return KS_STATUS_NO_RIGHTS;
        case KS_IPC_ERR_NO_MEM:  return KS_STATUS_OUT_OF_MEMORY;
        case KS_IPC_ERR_CLOSED:  return KS_STATUS_BUSY;
        case KS_IPC_ERR_EMPTY:   return KS_STATUS_BUSY;
        default:                 return KS_STATUS_INVALID_ARG;
    }
}

// ---- Memory ----

void *cka_kmalloc(size_t size)
{
    return kmalloc(size);
}

void cka_kfree(void *ptr)
{
    kfree(ptr);
}

static size_t round_up_pow2_size(size_t x)
{
    if (x <= 1) {
        return 1;
    }
    // Round up to the next power of two.
    size_t p = 1;
    while (p < x) {
        p <<= 1;
    }
    return p;
}

void *cka_malloc(size_t size, size_t alignment)
{
    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    alignment = round_up_pow2_size(alignment);

    // We store the original pointer in the word immediately before the
    // returned aligned pointer.
    size_t extra = (alignment - 1) + sizeof(void *);
    void *raw = cka_kmalloc(size + extra);
    if (!raw) {
        return NULL;
    }

    uintptr_t base = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (base + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

void cka_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    void *raw = ((void **)ptr)[-1];
    cka_kfree(raw);
}

void *cka_memcpy(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}

void *cka_memset(void *dst, int c, size_t n)
{
    return memset(dst, c, n);
}

// ---- Scheduling ----

void cka_yield(void)
{
    yield();
}

void cka_sleep_ticks(uint64_t ticks)
{
    while (ticks--) {
        yield();
    }
}

// ---- Capability operations ----

ks_status_t cka_cap_create(cap_type_t type, cap_rights_t rights, void *obj, cap_id_t *out_id)
{
    if (!g_core_caps || !out_id) {
        return KS_STATUS_INVALID_ARG;
    }
    return cap_create(g_core_caps, type, rights, obj, out_id);
}

ks_status_t cka_cap_retain(cap_id_t id)
{
    if (!g_core_caps) {
        return KS_STATUS_INVALID_STATE;
    }
    // Capabilities are table entries and are not reference-counted. "Retain"
    // is treated as a validity check so callers can safely gate later use.
    return cap_table_lookup(g_core_caps, (cap_handle_t)id, 0) ? KS_STATUS_OK
                                                          : KS_STATUS_NOT_FOUND;
}

ks_status_t cka_cap_release(cap_id_t id)
{
    if (!g_core_caps) {
        return KS_STATUS_INVALID_STATE;
    }
    cap_status_t st = cap_drop(g_core_caps, (cap_handle_t)id);
    switch (st) {
        case CAP_OK:             return KS_STATUS_OK;
        case CAP_ERR_INVALID:    return KS_STATUS_NOT_FOUND;
        case CAP_ERR_DENIED:     return KS_STATUS_NO_RIGHTS;
        case CAP_ERR_NO_SPACE:   return KS_STATUS_OUT_OF_MEMORY;
        default:                 return KS_STATUS_INTERNAL;
    }
}

// ---- IPC ----

ks_status_t cka_ipc_send(cap_id_t endpoint_cap, const ks_ipc_msg_t *msg)
{
    if (!g_core_caps || !msg) {
        return KS_STATUS_INVALID_ARG;
    }
    return ks_from_ipc_status(ipc_send_cap(g_core_caps, (cap_handle_t)endpoint_cap, msg));
}

ks_status_t cka_ipc_recv(cap_id_t endpoint_cap, ks_ipc_msg_t *msg)
{
    if (!g_core_caps || !msg) {
        return KS_STATUS_INVALID_ARG;
    }
    return ks_from_ipc_status(ipc_recv_cap(g_core_caps, (cap_handle_t)endpoint_cap, msg));
}
