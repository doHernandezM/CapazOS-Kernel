/*
 * swift_runtime_shims.c
 *
 * Minimal libc-symbol shims used by the Swift runtime when linking Core(Swift)
 * into the kernel image.
 *
 * The kernel is freestanding (no libc). Swift currently expects a small set of
 * POSIX/C runtime entrypoints (e.g. posix_memalign/free/putchar) when certain
 * runtime facilities are pulled in.
 *
 * These shims route through kernel_services_v1 so the kernel remains in control
 * of allocation and logging.
 */

#include <stddef.h>
#include <stdint.h>

#include "core_kernel_abi.h"
#include "core_kernel_abi_v3.h"

static const kernel_services_v1_t *g_services;
static const kernel_services_v3_t *g_services_v3;
// Shadow copy of the v1 subset for back-compat consumers.
// We keep a copy instead of casting a v3 pointer to v1 to avoid strict-aliasing UB.
static kernel_services_v1_t g_services_v1_shadow;

// Core is built freestanding (no libc), so <string.h> is not available.
// Provide a tiny memcpy for the one place we need it.
static inline void *core_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void core_set_services(const kernel_services_v1_t *services) {
    g_services = services;
}

void core_set_services_v3(const kernel_services_v3_t *services) {
    // Seed both the v3 pointer and the legacy v1 pointer. This allows older
    // Core code that only uses v1 (e.g. early logging) to continue working even
    // if the kernel only calls core_set_services_v3().
    g_services_v3 = services;
    if (!services) {
        g_services = NULL;
        return;
    }

    // v3's initial fields are ABI-compatible with v1; copy just that prefix.
    // This keeps legacy v1 consumers (e.g. early log) alive without depending on
    // nested struct layouts inside kernel_services_v3_t.
    core_memcpy(&g_services_v1_shadow, services, sizeof(kernel_services_v1_t));
    g_services = &g_services_v1_shadow;
}

const kernel_services_v1_t *core_services_v1(void) {
    return g_services;
}

const kernel_services_v3_t *core_services_v3(void) {
    return g_services_v3;
}

// ---------- Logging / stdio ----------

__attribute__((weak))
int putchar(int c) {
    if (g_services && g_services->log) {
        char buf[2];
        buf[0] = (char)c;
        buf[1] = '\0';
        g_services->log(buf);
    }
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
int posix_memalign(void **memptr, size_t alignment, size_t size) {
    if (!memptr) {
        return 22; // EINVAL
    }

    // POSIX requires alignment to be power-of-two and a multiple of sizeof(void*).
    if (!is_pow2(alignment) || (alignment % sizeof(void *)) != 0) {
        *memptr = NULL;
        return 22; // EINVAL
    }

    if (!g_services || !g_services->alloc) {
        *memptr = NULL;
        return 12; // ENOMEM
    }

    // Allocate enough space for alignment slack + header.
    size_t total = size + alignment + sizeof(shim_hdr_t);
    void *base = g_services->alloc(total, _Alignof(max_align_t));
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
void free(void *ptr) {
    if (!ptr) {
        return;
    }

    if (!g_services || !g_services->free) {
        return;
    }

    // If the pointer came from posix_memalign above, free the base.
    shim_hdr_t *hdr = (shim_hdr_t *)((uintptr_t)ptr - sizeof(shim_hdr_t));
    if (hdr->magic == SHIM_MAGIC && hdr->base) {
        g_services->free(hdr->base);
        return;
    }

    // Otherwise assume it is a direct kernel_services allocation.
    g_services->free(ptr);
}

// Optional convenience shims that some Swift runtime paths may use.
__attribute__((weak))
void *malloc(size_t size) {
    if (!g_services || !g_services->alloc) {
        return NULL;
    }
    return g_services->alloc(size, _Alignof(max_align_t));
}

__attribute__((weak))
void *calloc(size_t n, size_t size) {
    size_t total = n * size;
    void *p = malloc(total);
    if (!p) {
        return NULL;
    }
    // No memset in freestanding by default; do a simple byte loop.
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (size_t i = 0; i < total; i++) {
        b[i] = 0;
    }
    return p;
}
