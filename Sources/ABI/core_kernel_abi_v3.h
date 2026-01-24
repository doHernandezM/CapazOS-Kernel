// Kernel Services ABI v3
//
// v3 extends v2 with IPC entrypoints (capability-scoped endpoints).
// The first fields match the v2 layout so a v3 pointer may be treated as v2
// when only v2 features are used.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core_kernel_abi_v2.h"

#ifdef __cplusplus
extern "C" {
#endif

// IPC status codes (negative = error).
typedef int32_t ks_ipc_status_t;

enum {
    KS_IPC_OK = 0,
    KS_IPC_ERR_INVALID = -1,
    KS_IPC_ERR_RIGHTS  = -2,
    KS_IPC_ERR_NO_MEM  = -3,
    KS_IPC_ERR_EMPTY   = -4,
    KS_IPC_ERR_CLOSED  = -5,
};

// Fixed inline payload for M8 bring-up.
// Larger payloads will be supported later via MEMOBJ capability transfer.
#ifndef KS_IPC_MSG_MAX
#define KS_IPC_MSG_MAX 128u
#endif

typedef struct ks_ipc_msg {
    uint32_t tag;
    uint32_t len; // number of bytes valid in data[]
    uint8_t  data[KS_IPC_MSG_MAX];
} ks_ipc_msg_t;

// v3 services table.
typedef struct kernel_services_v3 {
    // v2 prefix (MUST NOT change order)
    uint32_t abi_version;
    uint32_t reserved0;
    void (*log)(const char *s);
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    void (*yield)(void);

    // v2 extensions (cap ops)
    ks_cap_status_t (*cap_dup)(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out);
    ks_cap_status_t (*cap_transfer)(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out);
    ks_cap_status_t (*cap_drop)(ks_cap_handle_t h);
    ks_cap_status_t (*cap_invalidate)(ks_cap_handle_t h);

    // v3 extensions (IPC)
    // Create an endpoint capability in the current task's cap-space.
    // Rights: CAP_R_SEND/CAP_R_RECV (and optional CAP_R_DUP/CAP_R_TRANSFER) are encoded in `rights`.
    ks_ipc_status_t (*endpoint_create)(ks_cap_rights_t rights, ks_cap_handle_t *out);

    // Send a message to an endpoint referenced by capability handle `endpoint`.
    // Contract:
    //  - Thread context only (no IRQ).
    //  - Kernel allocates an internal message object and copies msg->data[0..len).
    //  - Ownership of the internal message transfers to the receiver and is freed by the kernel
    //    once the receiver has copied it out via ipc_recv().
    ks_ipc_status_t (*ipc_send)(ks_cap_handle_t endpoint, const ks_ipc_msg_t *msg);

    // Receive the next message from `endpoint` into `out`.
    // Contract:
    //  - Thread context only (no IRQ).
    //  - Blocking: if the endpoint queue is empty, the calling thread blocks until a message arrives.
    //  - On success, the message is copied into *out and the internal message object is freed by the kernel.
    ks_ipc_status_t (*ipc_recv)(ks_cap_handle_t endpoint, ks_ipc_msg_t *out);
} kernel_services_v3_t;

// Kernel-side access to the v3 service table.
const kernel_services_v3_t *kernel_services_v3(void);

#ifdef __cplusplus
}
#endif
