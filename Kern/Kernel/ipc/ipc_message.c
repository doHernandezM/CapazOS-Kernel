#include "ipc/ipc_message.h"

#include <stdbool.h>

#include "alloc/slab_cache.h"
#include "contracts.h"
#include "debug/panic.h"
#include "mm/mem.h" // memset

static slab_cache_t g_ipc_msg_cache;
static bool s_ipc_msg_cache_inited = false;

void ipc_msg_cache_init(void) {
    if (s_ipc_msg_cache_inited) return;
    slab_cache_init(&g_ipc_msg_cache, "ipc_msg", sizeof(ipc_msg_t), (size_t)_Alignof(ipc_msg_t));
    s_ipc_msg_cache_inited = true;
}

bool ipc_msg_cache_get_stats(slab_cache_stats_t *out) {
    if (!s_ipc_msg_cache_inited) {
        return false;
    }
    return slab_cache_get_stats(&g_ipc_msg_cache, out);
}

ipc_msg_t *ipc_msg_alloc(void) {
    ASSERT_THREAD_CONTEXT();
    if (!s_ipc_msg_cache_inited) {
        panic("ipc_msg_alloc: cache not initialized");
    }
    ipc_msg_t *m = (ipc_msg_t *)slab_alloc(&g_ipc_msg_cache);
    if (!m) {
        panic("ipc_msg_alloc: OOM");
    }
    memset(m, 0, sizeof(*m));
    return m;
}

void ipc_msg_free(ipc_msg_t *m) {
    ASSERT_THREAD_CONTEXT();
    if (!m) return;
    if (!s_ipc_msg_cache_inited) {
        panic("ipc_msg_free: cache not initialized");
    }
    slab_free(&g_ipc_msg_cache, m);
}
