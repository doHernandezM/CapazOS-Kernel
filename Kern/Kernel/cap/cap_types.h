#pragma once

// Capability types are part of the kernel's internal object model.
// Core consumes them only through the internal Kernel API surface.

typedef enum cap_type {
    CAP_TYPE_INVALID = 0,

    CAP_TYPE_TASK,
    CAP_TYPE_THREAD,
    CAP_TYPE_VMO,
    CAP_TYPE_VMAP,
    CAP_TYPE_ENDPOINT,

    // NOTE: Do not add placeholder types here.
    // If a capability type exists, it should correspond to a real mechanism.
    // Policy/services live in Core.
} cap_type_t;
