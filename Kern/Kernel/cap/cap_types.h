#pragma once

#include <stdbool.h>
#include <stdint.h>

// Capability object types (mechanisms, not policy).
// Keep values stable once Core starts persisting/serializing handles.
// For now these are kernel-internal tags.
typedef enum cap_type {
    CAP_TYPE_INVALID = 0,

    CAP_TYPE_TASK,
    CAP_TYPE_THREAD,
    CAP_TYPE_ENDPOINT,
    CAP_TYPE_MEMOBJ,
    CAP_TYPE_IRQ_TOKEN,
    CAP_TYPE_TIMER_TOKEN,
    CAP_TYPE_SERVICE,

    CAP_TYPE__MAX
} cap_type_t;

static inline bool cap_type_is_valid(cap_type_t t) {
    return (t > CAP_TYPE_INVALID) && (t < CAP_TYPE__MAX);
}
