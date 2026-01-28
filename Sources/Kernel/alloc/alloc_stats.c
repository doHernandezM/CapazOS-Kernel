/*
 * alloc_stats.c — allocation observability aggregation.
 *
 * This module aggregates statistics from each allocator tier (PMM, slab caches,
 * kheap) into a single snapshot for debugging or Core introspection.
 */

#include "alloc/alloc_stats.h"

#include "cap/cap_entry.h"
#include "ipc/ipc_message.h"
#include "mm/mem.h"
#include "sched/thread.h"

bool kernel_get_alloc_stats(kernel_alloc_stats_t *out) {
    if (!out) {
        return false;
    }

    memset(out, 0, sizeof(*out));

    /* Buffer allocator and PMM are always present once initialized. */
    kheap_get_stats(&out->kheap);
    (void)pmm_get_stats_ex(&out->pmm);

    out->have_thread_cache = thread_cache_get_stats(&out->thread_cache);
    out->have_ipc_msg_cache = ipc_msg_cache_get_stats(&out->ipc_msg_cache);
    out->have_cap_entry_cache = cap_entry_cache_get_stats(&out->cap_entry_cache);

    return true;
}
