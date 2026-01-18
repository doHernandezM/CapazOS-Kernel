#pragma once
#include <stdint.h>

#include "alloc/slab_cache.h"

/* Minimal fixed-size capability entry (expanded in later milestones). */
typedef struct cap_entry {
    uint64_t object;   /* opaque object pointer/id */
    uint32_t rights;   /* rights bitmask */
    uint32_t type;     /* type tag */
    struct cap_entry *next;
} cap_entry_t;

/* M5.5: slab-backed cache for capability entries (kernel objects). */
void cap_entry_cache_init(void);
cap_entry_t *cap_entry_alloc(void);
void cap_entry_free(cap_entry_t *e);

/* Observability (M5.5 Phase 3). Returns false if cache not initialized. */
bool cap_entry_cache_get_stats(slab_cache_stats_t *out);
