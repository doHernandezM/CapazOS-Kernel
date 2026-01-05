#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Kernel heap layered on PMM.
 *
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

#endif /* KHEAP_H */
