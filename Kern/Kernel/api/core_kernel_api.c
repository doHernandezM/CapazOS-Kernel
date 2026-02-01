/*
 * core_kernel_api.c
 *
 * Implementation of the Core ↔ Kernel internal API. These functions
 * forward into kernel mechanisms such as UART logging, the heap, and
 * the scheduler. They intentionally avoid exposing kernel internal
 * structures and keep their contracts simple. See core_kernel_api.h
 * for documentation.
 */

#include "api/core_kernel_api.h"

#include <stddef.h>
#include <stdint.h>

#include "uart_pl011.h"    /* uart_putc */
#include "kheap.h"        /* kbuf_alloc/kbuf_free */
#include "sched/sched.h"  /* yield() */

/* Write a null‑terminated string to the UART. Characters are
 * transmitted one at a time via uart_putc to avoid requiring a
 * contiguous buffer. NULL is a no‑op. */
void cka_log_write(const char *str)
{
    if (!str) {
        return;
    }
    const char *p = str;
    while (*p) {
        uart_putc((int)*p++);
    }
}

/* Allocate memory from the kernel heap. Alignment must be a power of
 * two; if zero, default to the maximum alignment for the platform.
 * We do not currently support custom alignment in the underlying
 * allocator; small alignments are satisfied by kbuf_alloc. For
 * non‑standard alignments, we over‑allocate and adjust the returned
 * pointer. */
void *cka_malloc(size_t size, size_t alignment)
{
    if (size == 0) {
        return NULL;
    }
    /* Default to max_align_t if alignment is zero. */
    if (alignment == 0) {
        alignment = _Alignof(max_align_t);
    }
    /* If alignment is 1 or equal to max_align_t, we can use kbuf_alloc directly. */
    if (alignment <= _Alignof(max_align_t)) {
        return kbuf_alloc(size);
    }
    /* Over‑allocate: header + alignment slack. */
    size_t total = size + alignment + sizeof(void *);
    uint8_t *base = (uint8_t *)kbuf_alloc(total);
    if (!base) {
        return NULL;
    }
    uintptr_t p = (uintptr_t)(base + sizeof(void *));
    uintptr_t aligned = (p + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    /* Store the base pointer immediately before the aligned region so
     * cka_free can recover it. */
    void **hdr = (void **)(aligned - sizeof(void *));
    *hdr = base;
    return (void *)aligned;
}

/* Free memory previously returned by cka_malloc. If the pointer was
 * adjusted for alignment, recover the original base pointer. */
void cka_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    /* Recover potential over‑allocation header. */
    void **hdr = (void **)((uintptr_t)ptr - sizeof(void *));
    void *base = *hdr;
    /* If base lies within the over‑allocated region, free it; otherwise
     * treat ptr as the original base. */
    if (base) {
        kbuf_free(base);
    } else {
        kbuf_free(ptr);
    }
}

/* Minimal byte‑wise memcpy. */
void *cka_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

/* Minimal byte‑wise memset. */
void *cka_memset(void *ptr, int value, size_t n)
{
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)value;
    }
    return ptr;
}

/* Cooperative yield. */
void cka_yield(void)
{
    yield();
}
