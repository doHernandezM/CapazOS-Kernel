// intent_v1.h
// Intent descriptor ABI v1
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI versioning
#define CAPAZ_INTENT_ABI_V1 1u

typedef enum intent_class_v1 : uint32_t {
    INTENT_CLASS_INTERACTIVE = 0,
    INTENT_CLASS_BACKGROUND  = 1,
    INTENT_CLASS_BATCH       = 2,
    INTENT_CLASS_REALTIME    = 3,
} intent_class_t;

// Intent tag bitset (v1: reserved for future expansion).
typedef enum intent_tag_bits_v1 : uint32_t {
    INTENT_TAG_NONE = 0u,
    INTENT_TAG_IO   = 1u << 0,
    INTENT_TAG_UI   = 1u << 1,
    INTENT_TAG_ML   = 1u << 2,
    INTENT_TAG_GPU  = 1u << 3,
} intent_tags_t;

typedef struct intent_v1 {
    intent_class_t intent_class;
    uint32_t tags; // intent_tags_t bitset

    // Optional deadline. 0 means "no deadline".
    uint64_t deadline_ticks;

    // Optional CPU budget per epoch. 0 means "no budget specified".
    uint64_t cpu_budget_ticks_per_epoch;
} intent_t;

static inline intent_t intent_default_background(void) {
    intent_t i;
    i.intent_class = INTENT_CLASS_BACKGROUND;
    i.tags = INTENT_TAG_NONE;
    i.deadline_ticks = 0;
    i.cpu_budget_ticks_per_epoch = 0;
    return i;
}

#ifdef __cplusplus
} // extern "C"
#endif
