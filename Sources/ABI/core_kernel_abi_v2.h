// Kernel Services ABI v2
//
// v2 extends v1 with capability operations. The first fields match the v1
// layout so a v2 pointer may be treated as v1 when only v1 features are used.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "core_kernel_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

// Keep ABI-facing capability types separate from kernel-private capability
// types to avoid header-name collisions when building the kernel.
typedef uint64_t ks_cap_handle_t;
typedef uint32_t ks_cap_rights_t;
typedef int32_t ks_cap_status_t;

enum {
    KS_CAP_OK = 0,
    KS_CAP_ERR_INVALID = -1,
    KS_CAP_ERR_NO_RIGHTS = -2,
    KS_CAP_ERR_NO_SLOTS = -3,
    KS_CAP_ERR_OOM = -4,
    KS_CAP_ERR_UNSUPPORTED = -5,
};

typedef struct kernel_services_v2 {
    // v1 prefix (MUST NOT change order)
    uint32_t abi_version;
    uint32_t reserved0;
    void (*log)(const char *s);
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);
    void (*yield)(void);

    // v2 extensions
    ks_cap_status_t (*cap_dup)(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out);
    ks_cap_status_t (*cap_transfer)(ks_cap_handle_t h, ks_cap_rights_t mask, ks_cap_handle_t *out);
    ks_cap_status_t (*cap_drop)(ks_cap_handle_t h);
    ks_cap_status_t (*cap_invalidate)(ks_cap_handle_t h);
} kernel_services_v2_t;

// Kernel-side access to the v2 service table.
const kernel_services_v2_t *kernel_services_v2(void);

#ifdef __cplusplus
}
#endif
