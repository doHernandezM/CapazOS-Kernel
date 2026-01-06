#ifndef CAPAZ_DEADLINE_QUEUE_H
#define CAPAZ_DEADLINE_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Minimal deadline queue.
 *
 * Groundwork for later timer-based scheduling. This is deliberately tiny:
 * - Fixed capacity (no allocation)
 * - O(n) insert and O(n) peek (acceptable for now)
 * - No coalescing, no "intent" metadata yet
 */

#ifndef DLQ_MAX_ITEMS
#define DLQ_MAX_ITEMS 64
#endif

typedef struct {
    uint64_t deadline; /* absolute timestamp in clocksource ticks */
    void    *cookie;   /* opaque; unused for now */
} dlq_item_t;

typedef struct {
    dlq_item_t items[DLQ_MAX_ITEMS];
    uint32_t   count;
} deadline_queue_t;

void dlq_init(deadline_queue_t *q);

/* Returns false if the queue is full. */
bool dlq_push(deadline_queue_t *q, uint64_t deadline, void *cookie);

/* Returns false if empty. */
bool dlq_peek_next(const deadline_queue_t *q, dlq_item_t *out);

/* Returns false if empty. */
bool dlq_pop_next(deadline_queue_t *q, dlq_item_t *out);

#endif /* CAPAZ_DEADLINE_QUEUE_H */
