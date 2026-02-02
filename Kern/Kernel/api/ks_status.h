#pragma once

#include <stdint.h>

// General-purpose status codes for the Kernel↔Core internal API surface.
//
// This is *not* a versioned ABI. It is an internal convention used by both
// C-Core and Swift-Core when calling kernel mechanisms.
//
// Subsystems may also define more specific status enums (e.g. ks_ipc_status_t,
// ks_cap_status_t). When the internal API needs a unified status, translate
// subsystem errors to one of these values.

typedef int32_t ks_status_t;

enum {
    KS_STATUS_OK            = 0,

    // Caller errors
    KS_STATUS_INVALID_ARG   = -1,
    KS_STATUS_INVALID_STATE = -2,

    // Resource/permission errors
    KS_STATUS_NO_RIGHTS     = -3,
    KS_STATUS_NO_SPACE      = -4,
    KS_STATUS_OUT_OF_MEMORY = -5,

    // Lookup/feature errors
    KS_STATUS_NOT_FOUND     = -6,
    KS_STATUS_NOT_SUPPORTED = -7,

    // Transient failures
    KS_STATUS_BUSY          = -8,
    KS_STATUS_TIMED_OUT     = -9,

    // Catch-all
    KS_STATUS_INTERNAL      = -10,
};
