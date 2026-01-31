#pragma once
/*
 * slab_cache.h — simple slab/cache allocator (bring-up)
 *
 * Purpose:
 *  - Kernel OBJECTS only (fixed-size, type-specific caches)
 *  - Backed by PMM pages only
 *  - Thread-context only (no allocation/free in IRQ context)
 *
 * Notes:
 *  - Single-core bring-up: no locks.
 *  - Stats/introspection can be expanded later.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Per-cache observability (best-effort; expanded over time). */
typedef struct slab_cache_stats {
    uint64_t alloc_calls;
    uint64_t free_calls;
    uint64_t inuse_objects;
    uint64_t peak_inuse_objects;
    uint64_t slab_pages_allocated;
    uint64_t alloc_failures;
} slab_cache_stats_t;

typedef struct slab_page slab_page_t;

typedef struct slab_cache {
    const char *name;
    uint32_t    obj_size;   /* aligned object size */
    uint32_t    obj_align;  /* alignment used for objects */
    slab_page_t *pages;     /* singly-linked list of pages */

    /* Stats (best-effort; single-core bring-up, no locking). */
    uint64_t    alloc_calls;
    uint64_t    free_calls;
    uint64_t    inuse_objects;
    uint64_t    peak_inuse_objects;
    uint64_t    slab_pages_allocated;
    uint64_t    alloc_failures;
} slab_cache_t;

void slab_cache_init(slab_cache_t *c, const char *name, size_t obj_size, size_t align);
void *slab_alloc(slab_cache_t *c);
void slab_free(slab_cache_t *c, void *p);

/* Returns false on invalid args. */
bool slab_cache_get_stats(const slab_cache_t *c, slab_cache_stats_t *out);
