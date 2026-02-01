#pragma once

// Core-callable kernel mechanisms.
//
// This header is the (small) internal API boundary after merging Core+Kernel.
// It is intentionally not versioned; stability is maintained by repository
// discipline (see OS/Docs/BoundaryRules.md).
//
// Context rules are documented per function. Unless stated otherwise, functions
// are THREAD-CONTEXT ONLY (no IRQ).

#include <stddef.h>
#include <stdint.h>

#include "api/ks_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------- Logging ----------------

// THREAD-CONTEXT ONLY for now. Early bring-up routes to UART directly.
void cka_log_write(const char *cstr);

// ---------------- Allocation ----------------

// THREAD-CONTEXT ONLY.
void *cka_malloc(size_t size);
void  cka_free(void *ptr);

// ---------------- Scheduler ----------------

// Cooperative yield (THREAD-CONTEXT ONLY).
void cka_yield(void);

// ---------------- Capability operations ----------------

// These operate on the *current task's* capability space.
ks_cap_status_t cka_cap_dup(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out);
ks_cap_status_t cka_cap_transfer(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out);
ks_cap_status_t cka_cap_drop(ks_cap_handle_t h);
ks_cap_status_t cka_cap_invalidate(ks_cap_handle_t h);

// ---------------- IPC (endpoint capabilities) ----------------

// Create an endpoint capability in the current task.
// Rights bits are the kernel's CAP_R_SEND/CAP_R_RECV/etc.
ks_ipc_status_t cka_endpoint_create(ks_cap_rights_t rights, ks_cap_handle_t *out);

// Send/recv using endpoint capabilities in the current task.
ks_ipc_status_t cka_ipc_send(ks_cap_handle_t endpoint, const ks_ipc_msg_t *msg);
ks_ipc_status_t cka_ipc_recv(ks_cap_handle_t endpoint, ks_ipc_msg_t *out);

#ifdef __cplusplus
}
#endif
