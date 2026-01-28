#include "console/ringbuf.h"


void rx_ringbuf_init(rx_ringbuf_t *rb, uint8_t *storage, size_t storage_cap)
{
    if (!rb || !storage || storage_cap < 2) return;
    rb->buf = storage;
    rb->cap = storage_cap;
    rb->head = 0;
    rb->tail = 0;
    rb->overflowed = false;
}

static inline size_t rb_next(const rx_ringbuf_t *rb, size_t idx)
{
    idx++;
    if (idx >= rb->cap) idx = 0;
    return idx;
}

size_t rx_ringbuf_push_from_irq(rx_ringbuf_t *rb, const uint8_t *data, size_t n)
{
    if (!rb || !rb->buf || !data || n == 0) return 0;

    size_t pushed = 0;
    for (size_t i = 0; i < n; i++) {
        size_t next = rb_next(rb, rb->head);
        if (next == rb->tail) {
            // Full: drop oldest (advance tail) to make room.
            rb->overflowed = true;
            rb->tail = rb_next(rb, rb->tail);
        }
        rb->buf[rb->head] = data[i];
        rb->head = next;
        pushed++;
    }
    return pushed;
}

size_t rx_ringbuf_pop_in_thread(rx_ringbuf_t *rb, uint8_t *out, size_t max)
{
    if (!rb || !rb->buf || !out || max == 0) return 0;

    uint64_t daif = irq_save();
    size_t n = 0;
    while (n < max && rb->tail != rb->head) {
        out[n++] = rb->buf[rb->tail];
        rb->tail = rb_next(rb, rb->tail);
    }
    irq_restore(daif);
    return n;
}

size_t rx_ringbuf_count(rx_ringbuf_t *rb)
{
    if (!rb || !rb->buf) return 0;
    uint64_t daif = irq_save();
    size_t h = rb->head;
    size_t t = rb->tail;
    irq_restore(daif);

    if (h >= t) return h - t;
    return (rb->cap - t) + h;
}

bool rx_ringbuf_take_overflow(rx_ringbuf_t *rb)
{
    if (!rb) return false;
    uint64_t daif = irq_save();
    bool ov = rb->overflowed;
    rb->overflowed = false;
    irq_restore(daif);
    return ov;
}
