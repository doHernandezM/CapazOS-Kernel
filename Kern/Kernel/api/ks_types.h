#pragma once

// Shared, non-versioned types that are safe for Core (C/Swift) to import.
//
// This replaces the old OS/Kern/ABI/* headers. These types are still treated as
// stable contracts, but they are now internal to the single-project build.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------- Capability handles & rights ----------------

// Stable opaque handle type.
// Packing (v1): [gen:32][index:32]
typedef uint64_t ks_cap_handle_t;

// Rights bitset (matches cap_rights_t in kernel).
typedef uint32_t ks_cap_rights_t;

// Status codes (negative = error).
typedef int32_t ks_cap_status_t;

enum {
    KS_CAP_OK = 0,
    KS_CAP_ERR_INVALID = -1,
    KS_CAP_ERR_NO_RIGHTS = -2,
    KS_CAP_ERR_NO_SLOTS = -3,
    KS_CAP_ERR_OOM = -4,
    KS_CAP_ERR_UNSUPPORTED = -5,
};

// ---------------- IPC messages ----------------

typedef int32_t ks_ipc_status_t;

enum {
    KS_IPC_OK = 0,
    KS_IPC_ERR_INVALID = -1,
    KS_IPC_ERR_RIGHTS  = -2,
    KS_IPC_ERR_NO_MEM  = -3,
    KS_IPC_ERR_EMPTY   = -4,
    KS_IPC_ERR_CLOSED  = -5,
};

// Fixed inline payload for bring-up.
// Larger payloads can be supported later via a shared-memory/MEMOBJ capability.
#ifndef KS_IPC_MSG_MAX
#define KS_IPC_MSG_MAX 128u
#endif

typedef struct ks_ipc_msg {
    uint32_t tag;
    uint32_t len; // number of bytes valid in data[]
    uint8_t  data[KS_IPC_MSG_MAX];
} ks_ipc_msg_t;
