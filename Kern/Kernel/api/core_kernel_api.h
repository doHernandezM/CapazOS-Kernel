#pragma once

// Internal Core<->Kernel API surface.
//
// A0/A1 boundary rule: Kernel provides mechanisms; Core provides policy.
// This header is an internal API (not a versioned ABI) and may evolve with
// the repo as long as Kernel and Core are updated together.

#include <stddef.h>
#include <stdint.h>

#include "cap/cap_table.h"
#include "cap/cap_rights.h"
#include "cap/cap_types.h"
#include "ipc/endpoint.h"
#include "ks_status.h"

// Internal capability identifier type used across the Core<->Kernel API.
// The kernel's canonical handle type is cap_handle_t.
typedef cap_handle_t cap_id_t;

#ifdef __cplusplus
extern "C" {
#endif

// ---- Bring-up / attachment ----

// Called by Kernel during bootstrap to identify the Core task's capability
// space. Core-callable wrappers that operate on capability IDs use this table.
void cka_attach_core_caps(cap_table_t *core_caps);

// Returns the attached Core cap table (may be NULL if not attached yet).
cap_table_t *cka_core_caps(void);

// ---- Logging ----

void cka_early_log(const char *s);
void cka_console_write(const char *s, size_t len);

// Convenience helper for logging a NUL-terminated string.
// This is intentionally tiny and can be used from Core shims.
void cka_log_write(const char *s);

// ---- Memory ----

void *cka_kmalloc(size_t size);
void cka_kfree(void *ptr);

// Aligned allocation helpers for Core.
// These are intentionally minimal and exist primarily for Swift runtime shims.
void *cka_malloc(size_t size, size_t alignment);
void cka_free(void *ptr);

void *cka_memcpy(void *dst, const void *src, size_t n);
void *cka_memset(void *dst, int c, size_t n);

// ---- Scheduling ----

void cka_yield(void);

// Placeholder sleep hook. Until the kernel exposes a timer-based sleep,
// this yields in a loop.
void cka_sleep_ticks(uint64_t ticks);

// ---- Capability operations (Core-callable) ----

// Create and insert a capability into the Core task's cap table.
ks_status_t cka_cap_create(cap_type_t type, cap_rights_t rights, void *obj, cap_id_t *out_id);

ks_status_t cka_cap_retain(cap_id_t id);
ks_status_t cka_cap_release(cap_id_t id);

// ---- IPC (Core-callable) ----

ks_status_t cka_ipc_send(cap_id_t endpoint_cap, const ks_ipc_msg_t *msg);
ks_status_t cka_ipc_recv(cap_id_t endpoint_cap, ks_ipc_msg_t *msg);
ks_status_t cka_ipc_try_recv(cap_id_t endpoint_cap, ks_ipc_msg_t *msg);

// Kernel-only hook: attach the kernel log endpoint for forwarding.
void cka_attach_kernel_log_ep(cap_id_t endpoint_cap);

#ifdef __cplusplus
} // extern "C"
#endif
