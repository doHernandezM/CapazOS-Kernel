#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "alloc/slab_cache.h"
#include "cap/cap_rights.h"
#include "cap/cap_types.h"

/*
 * Slab-backed capability entry.
 *
 *  
 * The cap handle carries (index, generation); the entry stores its generation
 * and a validity flag.
 */
#define CAP_ENTRY_FLAG_VALID (1u << 0)

typedef struct cap_entry {
    cap_type_t type;       /* type tag */
    cap_rights_t rights;   /* rights bitmask */
    void *obj;             /* opaque kernel object pointer */
    uint32_t gen;          /* generation for stale-handle protection */
    uint32_t flags;        /* CAP_ENTRY_FLAG_* */
} cap_entry_t;

/* M5.5: slab-backed cache for capability entries (kernel objects). */
void cap_entry_cache_init(void);
cap_entry_t *cap_entry_alloc(void);
void cap_entry_free(cap_entry_t *e);

/* Observability (M5.5 Phase 3). Returns false if cache not initialized. */
bool cap_entry_cache_get_stats(slab_cache_stats_t *out);
