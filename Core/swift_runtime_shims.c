/*
 * swift_runtime_shims.c
 *
 * Minimal libc-symbol shims used by the Swift runtime when linking
 * Core (Swift) into the kernel image. The kernel is freestanding (no
 * libc). Swift currently expects a small set of POSIX/C runtime
 * entrypoints (e.g. posix_memalign/free/putchar) when certain runtime
 * facilities are pulled in.
 *
 * These shims route through the internal Core–Kernel API so that
 * allocation and logging go through kernel mechanisms. The old
 * services table ABI is removed; Core calls directly into kernel
 * functions declared in api/core_kernel_api.h.
 */

#include <stddef.h>
#include <stdint.h>

#include "api/core_kernel_api.h"


/* Weak putchar used by the Swift runtime. Writes a single character
 * via cka_log_write. We wrap the character in a one‑byte string. */
__attribute__((weak))
int putchar(int c) {
    char buf[2];
    buf[0] = (char)c;
    buf[1] = '\0';
    cka_log_write(buf);
    return c;
}

// ---------- Allocation helpers ----------

/* A tiny header placed immediately before the aligned pointer. The
 * magic value allows free() to detect whether the pointer was
 * over‑allocated by posix_memalign. */
typedef struct {
    uint64_t magic;
    void *base;
} shim_hdr_t;

/* 'CAPZALGN' in ASCII. */
static const uint64_t SHIM_MAGIC = 0x4341505A414C474EULL;

static int is_pow2(size_t x) {
    return x && ((x & (x - 1)) == 0);
}

/* Allocate aligned memory for the Swift runtime. We use cka_malloc to
 * obtain a buffer and adjust it to the requested alignment. A small
 * header records the base pointer for free(). */
__attribute__((weak))
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr) {
        return 22; // EINVAL
    }
    /* POSIX requires alignment to be power‑of‑two and a multiple of
     * sizeof(void*). */
    if (!is_pow2(alignment) || (alignment % sizeof(void *) != 0)) {
        *memptr = NULL;
        return 22; // EINVAL
    }
    /* Allocate enough space for alignment slack + header. */
    size_t total = size + alignment + sizeof(shim_hdr_t);
    void *base = cka_malloc(total, _Alignof(max_align_t));
    if (!base) {
        *memptr = NULL;
        return 12; // ENOMEM
    }
    uintptr_t p = (uintptr_t)base + sizeof(shim_hdr_t);
    uintptr_t aligned = (p + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    shim_hdr_t *hdr = (shim_hdr_t *)(aligned - sizeof(shim_hdr_t));
    hdr->magic = SHIM_MAGIC;
    hdr->base = base;
    *memptr = (void *)aligned;
    return 0;
}

/* Free memory previously allocated via posix_memalign or cka_malloc.
 * Detects the header to free the correct base pointer. */
__attribute__((weak))
void free(void *ptr) {
    if (!ptr) {
        return;
    }
    /* If the pointer came from posix_memalign above, free the base. */
    shim_hdr_t *hdr = (shim_hdr_t *)((uintptr_t)ptr - sizeof(shim_hdr_t));
    if (hdr->magic == SHIM_MAGIC && hdr->base) {
        cka_free(hdr->base);
        return;
    }
    /* Otherwise assume it is a direct allocation. */
    cka_free(ptr);
}

/* Convenience malloc wrapper. Uses the maximum alignment. */
__attribute__((weak))
void *malloc(size_t size) {
    return cka_malloc(size, _Alignof(max_align_t));
}

/* Convenience calloc wrapper. Allocates and zeroes the buffer. */
__attribute__((weak))
void *calloc(size_t n, size_t size) {
    size_t total = n * size;
    void *p = cka_malloc(total, _Alignof(max_align_t));
    if (!p) {
        return NULL;
    }
    cka_memset(p, 0, total);
    return p;
}
