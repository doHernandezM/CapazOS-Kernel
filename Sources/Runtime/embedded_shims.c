//
//  embedded_shims.c
//  KernelImage
//
//  Created by Cosas on 12/20/25.
//

#include <stdint.h>
#include <stddef.h>
#include "uart_pl011.h"

// -----------------------------
// Minimal heap for Swift runtime
// -----------------------------
static uint8_t g_heap[256 * 1024];   // bump heap (tune later)
static size_t g_heap_off = 0;

static size_t align_up(size_t x, size_t a) {
    return (x + (a - 1)) & ~(a - 1);
}

// POSIX: return 0 on success; error code otherwise.
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr) return 22; // EINVAL
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) return 22;

    size_t off = align_up(g_heap_off, alignment);
    if (off + size > sizeof(g_heap)) return 12; // ENOMEM

    *memptr = (void*)(g_heap + off);
    g_heap_off = off + size;
    return 0;
}

// For early bring-up: no-op free (bump allocator can't reclaim).
void free(void *ptr) { (void)ptr; }

// -----------------------------
// Basic output used by print()
// -----------------------------
int putchar(int c) {
    uart_putc((char)c);
    return c;
}

// -----------------------------
// memmove (and friends)
// -----------------------------
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;

    if (d == s || n == 0) return dst;

    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i != 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}

