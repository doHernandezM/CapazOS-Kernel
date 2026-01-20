#include "work_queue.h"

#include "alloc/slab_cache.h"
#include "contracts.h"
#include "irq.h"
#include "mm/mem.h"

/*
 * Work item cache (thread-context only).
 * IRQ must never allocate; allocate nodes ahead of time.
 */
static slab_cache_t g_work_item_cache;
static bool g_cache_inited = false;

void work_item_cache_init(void)
{
    ASSERT_THREAD_CONTEXT();
    if (g_cache_inited) return;

    slab_cache_init(&g_work_item_cache,
                    "work_item",
                    sizeof(work_item_t),
                    _Alignof(work_item_t));

    g_cache_inited = true;
}

work_item_t *work_item_alloc(work_fn_t fn, void *arg)
{
    ASSERT_THREAD_CONTEXT();
    if (!g_cache_inited) work_item_cache_init();

    work_item_t *it = (work_item_t *)slab_alloc(&g_work_item_cache);
    if (!it) return NULL;

    (void)memset(it, 0, sizeof(*it));
    it->fn = fn;
    it->arg = arg;
    it->next = NULL;
    return it;
}

void work_item_free(work_item_t *item)
{
    if (!item) return;
    ASSERT_THREAD_CONTEXT();
    slab_free(&g_work_item_cache, item);
}

void workq_init(workq_t *q)
{
    if (!q) return;
    q->head = NULL;
    q->tail = NULL;
}

bool workq_enqueue_from_irq(workq_t *q, work_item_t *item)
{
    ASSERT_IRQ_CONTEXT();
    if (!q || !item || !item->fn) return false;

    item->next = NULL;

    uint64_t flags = irq_save();
    if (q->tail) {
        q->tail->next = item;
        q->tail = item;
    } else {
        q->head = item;
        q->tail = item;
    }
    irq_restore(flags);
    return true;
}

work_item_t *workq_dequeue(workq_t *q)
{
    ASSERT_THREAD_CONTEXT();
    if (!q) return NULL;

    uint64_t flags = irq_save();
    work_item_t *it = q->head;
    if (it) {
        q->head = it->next;
        if (q->head == NULL) q->tail = NULL;
        it->next = NULL;
    }
    irq_restore(flags);
    return it;
}
