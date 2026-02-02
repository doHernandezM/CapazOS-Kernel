#pragma once

#include <stdint.h>

// Generic intent descriptor definitions (Phase 2).

typedef enum ks_intent_dir {
    KS_INTENT_DIR_CALL = 1,
    KS_INTENT_DIR_EVENT = 2
} ks_intent_dir_t;

typedef struct ks_intent_desc {
    uint32_t intent_id;
    uint32_t direction;   // ks_intent_dir_t
    uint32_t max_len;     // maximum payload length
    uint32_t rights;      // required cap_rights_t bits
} ks_intent_desc_t;

