/*
 * ks_types.h
 *
 * Shared type definitions for the Core↔Kernel internal API. These types
 * replace the old versioned ABI structures. They are POD and use
 * explicit integer widths to avoid layout ambiguity. Values are kept
 * intentionally simple for bring‑up; grow the enums and constants as
 * additional statuses and fields are required.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* Maximum inline payload size for IPC messages.  Matches the
 * previous IPC_MSG_INLINE_MAX constant (128 bytes) used by the
 * kernel message cache. Larger payloads will be supported later via
 * MEMOBJ capabilities. */
#ifndef KS_IPC_MSG_MAX
#define KS_IPC_MSG_MAX 128u
#endif

/* Kernel–services IPC message. Core uses this structure to send and
 * receive messages via capability–scoped endpoints. */
typedef struct ks_ipc_msg {
    uint32_t tag;              /* opaque tag for message type */
    uint32_t len;              /* number of valid bytes in data[] */
    uint8_t  data[KS_IPC_MSG_MAX];
} ks_ipc_msg_t;

/* IPC status codes returned by endpoint operations. Keep these
 * definitions stable; Core may interpret them. */
typedef enum ks_ipc_status {
    KS_IPC_OK = 0,
    KS_IPC_ERR_INVALID = 1,
    KS_IPC_ERR_RIGHTS = 2,
    KS_IPC_ERR_NO_MEM = 3,
    KS_IPC_ERR_CLOSED = 4
} ks_ipc_status_t;

/* Capability status codes returned by capability operations. */
typedef enum ks_cap_status {
    KS_CAP_OK = 0,
    KS_CAP_ERR_INVALID = 1,
    KS_CAP_ERR_NO_RIGHTS = 2,
    KS_CAP_ERR_NO_SLOTS = 3,
    KS_CAP_ERR_OOM = 4
} ks_cap_status_t;

/* Optional alias for capability handles passed across the API. A
 * capability handle is a 64‑bit integer index into a task’s cap table.
 * Kernel code continues to use cap_handle_t; Core code may use this
 * alias if desired. */
typedef uint64_t ks_cap_handle_t;
