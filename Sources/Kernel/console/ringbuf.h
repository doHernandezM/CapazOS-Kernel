#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "irq.h"

/*
 * IRQ-safe byte ring buffer.
 *
 * Design target for Phase 1:
 * - Single producer in IRQ context (push_from_irq).
 * - Single consumer in thread context (pop_in_thread).
 * - No allocation.
 * - Consumer side masks IRQs for short critical sections.
 */
typedef struct rx_ringbuf {
    uint8_t *buf;
    size_t   cap;   // number of bytes in buf (must be >= 2)
    size_t   head;  // producer writes at head
    size_t   tail;  // consumer reads at tail
    bool     overflowed;
} rx_ringbuf_t;

void rx_ringbuf_init(rx_ringbuf_t *rb, uint8_t *storage, size_t storage_cap);

// Producer: callable from IRQ handler.
size_t rx_ringbuf_push_from_irq(rx_ringbuf_t *rb, const uint8_t *data, size_t n);

// Consumer: callable from thread context. Masks IRQs briefly.
size_t rx_ringbuf_pop_in_thread(rx_ringbuf_t *rb, uint8_t *out, size_t max);

// Approximate count (masks IRQs briefly).
size_t rx_ringbuf_count(rx_ringbuf_t *rb);

// Clears overflow flag (masks IRQs briefly).
bool rx_ringbuf_take_overflow(rx_ringbuf_t *rb);
