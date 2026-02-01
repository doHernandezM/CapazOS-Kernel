/*
 * swift_runtime_shims.c
 *
 * Minimal libc-symbol shims used by the Swift runtime when linking Core(Swift)
 * into the kernel image.
 *
 * The kernel is freestanding (no libc). Swift expects a small set of C runtime
 * entrypoints (e.g. posix_memalign/free/putchar) when some runtime facilities
 * are pulled in.
 *
 * After the Kernel+Core merge, these shims call kernel primitives directly via
 * the internal Core-callable API (no versioned services table).
 */

#include <stddef.h>
#include <stdint.h>

#include "api/core_kernel_api.h"

// ---------- Logging / stdio ----------

__attribute__((weak))
int putchar(int c)
{
    char buf[2];
    buf[0] = (char)c;
    buf[1] = '\0';
    cka_log_write(buf);
    return c;
}

// ---------- Allocation ----------

// A tiny header placed immediately before the aligned pointer.
typedef struct {
    uint64_t magic;
    void *base;
} shim_hdr_t;

// 'CAPZALGN' in ASCII.
static const uint64_t SHIM_MAGIC = 0x4341505A414C474EULL;

static int is_pow2(size_t x) {
    return x && ((x & (x - 1)) == 0);
}

__attribute__((weak))
int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    if (!memptr) {
        return 22; // EINVAL
    }

    // POSIX requires alignment to be power-of-two and a multiple of sizeof(void*).
    if (!is_pow2(alignment) || (alignment % sizeof(void *)) != 0) {
        *memptr = NULL;
        return 22; // EINVAL
    }

    // Allocate enough space for alignment slack + header.
    size_t total = size + alignment + sizeof(shim_hdr_t);
    void *base = cka_malloc(total);
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

__attribute__((weak))
void free(void *ptr)
{
    if (!ptr) {
        return;
    }

    // If the pointer came from posix_memalign above, free the base.
    shim_hdr_t *hdr = (shim_hdr_t *)((uintptr_t)ptr - sizeof(shim_hdr_t));
    if (hdr->magic == SHIM_MAGIC && hdr->base) {
        cka_free(hdr->base);
        return;
    }

    // Otherwise assume it is a direct allocation.
    cka_free(ptr);
}

__attribute__((weak))
void *malloc(size_t size)
{
    return cka_malloc(size);
}

__attribute__((weak))
void *calloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p = malloc(total);
    if (!p) {
        return NULL;
    }

    // Freestanding memset is not guaranteed; do a simple byte loop.
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (size_t i = 0; i < total; i++) {
        b[i] = 0;
    }
    return p;
}
