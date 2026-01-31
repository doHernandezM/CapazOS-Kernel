#pragma once
/*
 * work_queue.h — Deferred work queue.
 *
 * Design (single-core bring-up):
 *  - Simple FIFO queue protected by irq_save()/irq_restore().
 *  - IRQ context: enqueue only (must not allocate).
 *  - Thread context: dequeue and execute callbacks.
 *
 * This queue allows deferred processing of work items posted from interrupt
 * context to be executed safely in thread context.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*work_fn_t)(void *arg);

typedef struct work_item {
    work_fn_t fn;
    void *arg;
    struct work_item *next;
} work_item_t;

typedef struct workq {
    work_item_t *head;
    work_item_t *tail;
} workq_t;

// Global deferred work queue.
// Drivers/ISRs can enqueue short tasks here to be processed in thread context.
extern workq_t g_deferred_workq;

/* Queue API */
void workq_init(workq_t *q);

/*
 * Enqueue from IRQ context only.
 * Returns true if enqueued; false if item was not enqueued.
 *
 * NOTE: Callers must ensure the same item isn't enqueued concurrently.
 */
bool workq_enqueue_from_irq(workq_t *q, work_item_t *item);

/* Dequeue from thread context only. Returns NULL if empty. */
work_item_t *workq_dequeue(workq_t *q);

/*
 * Work item cache (thread-context only): allocate nodes ahead of time.
 * IRQ path must never allocate.
 */
void work_item_cache_init(void);
work_item_t *work_item_alloc(work_fn_t fn, void *arg);
void work_item_free(work_item_t *item);

#ifdef __cplusplus
}
#endif
