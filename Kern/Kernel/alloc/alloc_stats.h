#pragma once

/*
 * alloc_stats.h — allocation observability aggregation.
 *
 * Provides a pure getter surface that can be used by debug commands or by Core
 * via a future ABI addition to query allocator statistics. No printing is required.
 */

#include <stdbool.h>

#include "alloc/slab_cache.h"
#include "kheap.h"
#include "mm/pmm.h"

typedef struct kernel_alloc_stats {
    /* Slab caches (kernel objects). */
    bool have_thread_cache;
    bool have_ipc_msg_cache;
    bool have_cap_entry_cache;
    slab_cache_stats_t thread_cache;
    slab_cache_stats_t ipc_msg_cache;
    slab_cache_stats_t cap_entry_cache;

    /* Buffer allocator (variable sized). */
    kheap_stats_t kheap;

    /* Page allocator. */
    pmm_stats_ex_t pmm;
} kernel_alloc_stats_t;

/* Best-effort snapshot. Returns false only on invalid args. */
bool kernel_get_alloc_stats(kernel_alloc_stats_t *out);
