#include "cap/cap_entry.h"

#include <stdbool.h>

#include "alloc/slab_cache.h"
#include "contracts.h"
#include "debug/panic.h"
#include "mm/mem.h" // memset

static slab_cache_t g_cap_entry_cache;
static bool s_cap_entry_cache_inited = false;

void cap_entry_cache_init(void) {
    if (s_cap_entry_cache_inited) return;
    slab_cache_init(&g_cap_entry_cache, "cap_entry", sizeof(cap_entry_t), (size_t)_Alignof(cap_entry_t));
    s_cap_entry_cache_inited = true;
}

bool cap_entry_cache_get_stats(slab_cache_stats_t *out) {
    if (!s_cap_entry_cache_inited) {
        return false;
    }
    return slab_cache_get_stats(&g_cap_entry_cache, out);
}

cap_entry_t *cap_entry_alloc(void) {
    ASSERT_THREAD_CONTEXT();
    if (!s_cap_entry_cache_inited) {
        panic("cap_entry_alloc: cache not initialized");
    }
    cap_entry_t *e = (cap_entry_t *)slab_alloc(&g_cap_entry_cache);
    if (!e) {
        return NULL;
    }
    memset(e, 0, sizeof(*e));
    return e;
}

void cap_entry_free(cap_entry_t *e) {
    ASSERT_THREAD_CONTEXT();
    if (!e) return;
    if (!s_cap_entry_cache_inited) {
        panic("cap_entry_free: cache not initialized");
    }
    slab_free(&g_cap_entry_cache, e);
}
