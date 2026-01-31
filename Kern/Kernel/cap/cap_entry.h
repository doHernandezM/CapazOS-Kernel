#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "alloc/slab_cache.h"
#include "cap/cap_rights.h"
#include "cap/cap_types.h"

/*
 * Slab-backed capability entry.
 *
 * Each capability is represented by a handle consisting of an index and a generation.
 * The corresponding cap_entry stores metadata (type, rights, object pointer, generation)
 * and a validity flag to protect against stale handle reuse.
 */
#define CAP_ENTRY_FLAG_VALID (1u << 0)

typedef struct cap_entry {
    cap_type_t type;       /* type tag */
    cap_rights_t rights;   /* rights bitmask */
    void *obj;             /* opaque kernel object pointer */
    uint32_t gen;          /* generation for stale-handle protection */
    uint32_t flags;        /* CAP_ENTRY_FLAG_* */
} cap_entry_t;

/* Initialize the slab-backed cache for capability entries (kernel objects). */
void cap_entry_cache_init(void);
cap_entry_t *cap_entry_alloc(void);
void cap_entry_free(cap_entry_t *e);

/* Returns false if cache not initialized. */
bool cap_entry_cache_get_stats(slab_cache_stats_t *out);
