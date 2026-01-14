#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Allocation policy (Milestone M4.5)
 *
 * - kmalloc/kfree (kheap) are for VARIABLE-SIZED BUFFERS.
 * - Kernel OBJECTS should migrate to type-specific allocators (slabs/caches).
 * - Allocation is THREAD CONTEXT ONLY: IRQ context must not allocate.

 * Phase A: page-granularity allocations via PMM.
 * Phase B: small-object allocator (buckets) backed by PMM pages.
 *
 * Single-core initially (no locks).
 */

void kheap_init(void);

/* Page-granularity API. Returns direct-mapped VA or NULL. */
void *kheap_alloc_pages(uint32_t pages, uint64_t *out_pa);
void  kheap_free_pages(void *va, uint32_t pages);

/* Convenience heap API. kmalloc/kfree handle both small and large sizes. */
void *kmalloc(size_t size);
void  kfree(void *ptr);


typedef struct kheap_stats {
    uint64_t cur_bytes;
    uint64_t peak_bytes;
    uint64_t big_alloc_calls;
    uint64_t big_free_calls;
    uint64_t fail_calls;
} kheap_stats_t;

/* Best-effort stats for bring-up/hardening. */
void kheap_get_stats(kheap_stats_t *out);

#endif /* KHEAP_H */
