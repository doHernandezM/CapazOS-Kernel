#include "deadline_queue.h"

static int32_t dlq_find_min_index(const deadline_queue_t *q)
{
    if (!q || q->count == 0) {
        return -1;
    }

    uint32_t best = 0;
    uint64_t best_deadline = q->items[0].deadline;
    for (uint32_t i = 1; i < q->count; i++) {
        if (q->items[i].deadline < best_deadline) {
            best = i;
            best_deadline = q->items[i].deadline;
        }
    }
    return (int32_t)best;
}

void dlq_init(deadline_queue_t *q)
{
    if (!q) {
        return;
    }
    q->count = 0;
}

bool dlq_push(deadline_queue_t *q, uint64_t deadline, void *cookie)
{
    if (!q) {
        return false;
    }
    if (q->count >= DLQ_MAX_ITEMS) {
        return false;
    }
    q->items[q->count].deadline = deadline;
    q->items[q->count].cookie   = cookie;
    q->count++;
    return true;
}

bool dlq_peek_next(const deadline_queue_t *q, dlq_item_t *out)
{
    if (!q || !out) {
        return false;
    }
    int32_t idx = dlq_find_min_index(q);
    if (idx < 0) {
        return false;
    }
    *out = q->items[(uint32_t)idx];
    return true;
}

bool dlq_pop_next(deadline_queue_t *q, dlq_item_t *out)
{
    if (!q || !out) {
        return false;
    }
    int32_t idx = dlq_find_min_index(q);
    if (idx < 0) {
        return false;
    }
    uint32_t uidx = (uint32_t)idx;
    *out = q->items[uidx];

    /* Remove by swapping with last element (order not preserved). */
    q->count--;
    if (uidx != q->count) {
        q->items[uidx] = q->items[q->count];
    }
    return true;
}
