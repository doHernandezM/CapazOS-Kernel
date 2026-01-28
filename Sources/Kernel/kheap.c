#include "kheap.h"

#include <stdbool.h>
#include <stdint.h>

#include "pmm.h"
#include "uart_pl011.h"
#include "mem.h"
#include "contracts.h"

#ifndef KMAIN_DEBUG
#define KMAIN_DEBUG 0
#endif

#define PAGE_SIZE 0x1000ULL

typedef struct free_node {
    struct free_node *next;
} free_node_t;

enum { NUM_BUCKETS = 8 };
static const uint16_t g_bucket_sizes[NUM_BUCKETS] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};

#define SLAB_MAGIC 0x534C4142u /* 'SLAB' */
#define BIG_MAGIC  0x42494721u /* 'BIG!' */

typedef struct slab_page_hdr {
    uint32_t magic;
    uint16_t bucket_index;
    uint16_t block_size;
} slab_page_hdr_t;

typedef struct big_alloc_hdr {
    uint32_t magic;
    uint32_t pages;
} big_alloc_hdr_t;

static free_node_t *g_freelist[NUM_BUCKETS];

/* Hardening: allocation counters and peak usage. */
static uint64_t g_kheap_cur_bytes = 0;
static uint64_t g_kheap_peak_bytes = 0;
static uint64_t g_kheap_small_allocs[NUM_BUCKETS];
static uint64_t g_kheap_small_frees[NUM_BUCKETS];
static uint64_t g_kheap_big_alloc_calls = 0;
static uint64_t g_kheap_big_free_calls = 0;
static uint64_t g_kheap_fail_calls = 0;
static uint64_t g_kheap_kmalloc_calls = 0;
static uint64_t g_kheap_kfree_calls = 0;
static uint64_t g_kheap_bucket_refills[NUM_BUCKETS] = {0};

static inline void kheap_account_alloc(uint64_t bytes) {
    g_kheap_cur_bytes += bytes;
    if (g_kheap_cur_bytes > g_kheap_peak_bytes) g_kheap_peak_bytes = g_kheap_cur_bytes;
}
static inline void kheap_account_free(uint64_t bytes) {
    if (g_kheap_cur_bytes >= bytes) g_kheap_cur_bytes -= bytes;
    else g_kheap_cur_bytes = 0;
}

#define KHEAP_POISON_BYTE 0xA5


static inline uint64_t align_down_4k(uint64_t x) { return x & ~(PAGE_SIZE - 1ULL); }
static inline uint64_t align_up_4k(uint64_t x)   { return (x + (PAGE_SIZE - 1ULL)) & ~(PAGE_SIZE - 1ULL); }

static int bucket_for_size(size_t size)
{
    for (int i = 0; i < NUM_BUCKETS; i++) {
        if (size <= (size_t)g_bucket_sizes[i]) {
            return i;
        }
    }
    return -1;
}

static void refill_bucket(int b)
{
    if ((unsigned)b < NUM_BUCKETS) g_kheap_bucket_refills[b]++;
    uint64_t page_pa = 0;
    void *page_va = pmm_alloc_page_va(&page_pa);
    if (!page_va) {
        return;
    }

    uint8_t *base = (uint8_t *)page_va;
    slab_page_hdr_t *hdr = (slab_page_hdr_t *)base;
    hdr->magic = SLAB_MAGIC;
    hdr->bucket_index = (uint16_t)b;
    hdr->block_size = g_bucket_sizes[b];

    uint64_t start = (uint64_t)(uintptr_t)(base + sizeof(slab_page_hdr_t));
    uint64_t bs = (uint64_t)hdr->block_size;
    /* Align the first block to its own size for clean alignment. */
    start = (start + (bs - 1ULL)) & ~(bs - 1ULL);
    uint64_t end = (uint64_t)(uintptr_t)(base + PAGE_SIZE);

    for (uint64_t p = start; p + bs <= end; p += bs) {
        free_node_t *n = (free_node_t *)(uintptr_t)p;
        n->next = g_freelist[b];
        g_freelist[b] = n;
    }
}

void kheap_init(void)
{
    for (int i = 0; i < NUM_BUCKETS; i++) {
        g_freelist[i] = 0;
    }

#if KMAIN_DEBUG
    uart_puts("KHEAP: init\n");
#endif
}

void *kheap_alloc_pages(uint32_t pages, uint64_t *out_pa)
{
    
    ASSERT_THREAD_CONTEXT();
if (pages == 0) return 0;
    uint64_t pa = 0;
    if (!pmm_alloc_pages(pages, &pa)) {
        g_kheap_fail_calls++; return 0;
    }
    if (out_pa) *out_pa = pa;
    return (void *)(uintptr_t)pmm_phys_to_virt(pa);
}

void kheap_free_pages(void *va, uint32_t pages)
{
    
    ASSERT_THREAD_CONTEXT();
if (!va || pages == 0) return;
    uint64_t pa0 = pmm_virt_to_phys((uint64_t)(uintptr_t)va);
    pa0 = align_down_4k(pa0);
    for (uint32_t i = 0; i < pages; i++) {
        pmm_free_page(pa0 + ((uint64_t)i * PAGE_SIZE));
    }
}

void *kmalloc(size_t size)
{
    ASSERT_THREAD_CONTEXT();
    g_kheap_kmalloc_calls++;
    if (size == 0) return 0;

    /* Small-object fast path: fixed buckets. */
    int b = bucket_for_size(size);
    if (b >= 0) {
        if (!g_freelist[b]) {
            refill_bucket(b);
        }
        free_node_t *n = g_freelist[b];
        if (!n) {
            return 0;
        }
        g_freelist[b] = n->next;
        g_kheap_small_allocs[b]++;
        kheap_account_alloc((uint64_t)g_bucket_sizes[b]);
        return (void *)n;
    }

    /* Large allocation: page-granularity, with a small header for kfree. */
    uint64_t total = (uint64_t)size + (uint64_t)sizeof(big_alloc_hdr_t);
    uint32_t pages = (uint32_t)(align_up_4k(total) / PAGE_SIZE);
    uint64_t pa = 0;
    void *base_va = kheap_alloc_pages(pages, &pa);
    if (!base_va) return 0;

    big_alloc_hdr_t *hdr = (big_alloc_hdr_t *)base_va;
    hdr->magic = BIG_MAGIC;
    hdr->pages = pages;
    g_kheap_big_alloc_calls++;
    kheap_account_alloc((uint64_t)pages * PAGE_SIZE);

    return (void *)(uintptr_t)((uint8_t *)base_va + sizeof(big_alloc_hdr_t));
}

void kfree(void *ptr)
{
    ASSERT_THREAD_CONTEXT();
    g_kheap_kfree_calls++;
    if (!ptr) return;

    uint64_t va = (uint64_t)(uintptr_t)ptr;
    uint64_t page_va = align_down_4k(va);

    uint32_t magic = *(const uint32_t *)(uintptr_t)page_va;
    if (magic == SLAB_MAGIC) {
        const slab_page_hdr_t *hdr = (const slab_page_hdr_t *)(uintptr_t)page_va;
        uint16_t b = hdr->bucket_index;
        if (b >= NUM_BUCKETS) return;
        /* Poison freed memory (basic UAF detection). */
        memset(ptr, KHEAP_POISON_BYTE, (size_t)hdr->block_size);
        free_node_t *n = (free_node_t *)ptr;
        n->next = g_freelist[b];
        g_freelist[b] = n;
        g_kheap_small_frees[b]++;
        kheap_account_free((uint64_t)hdr->block_size);
        return;
    }

    if (magic == BIG_MAGIC) {
        const big_alloc_hdr_t *hdr = (const big_alloc_hdr_t *)(uintptr_t)page_va;
        uint32_t pages = hdr->pages;
        if (pages == 0) return;
        /* Poison freed pages (basic UAF detection). */
        memset((void *)(uintptr_t)page_va, KHEAP_POISON_BYTE, (size_t)pages * (size_t)PAGE_SIZE);
        g_kheap_big_free_calls++;
        kheap_account_free((uint64_t)pages * PAGE_SIZE);
        kheap_free_pages((void *)(uintptr_t)page_va, pages);
        return;
    }

    /* Unknown pointer; ignore for now. */
}


void kheap_get_stats(kheap_stats_t *out)
{
    if (!out) return;
    out->cur_bytes = g_kheap_cur_bytes;
    out->peak_bytes = g_kheap_peak_bytes;
    out->kmalloc_calls = g_kheap_kmalloc_calls;
    out->kfree_calls = g_kheap_kfree_calls;
    out->big_alloc_calls = g_kheap_big_alloc_calls;
    out->big_free_calls = g_kheap_big_free_calls;
    out->fail_calls = g_kheap_fail_calls;
    for (int i = 0; i < NUM_BUCKETS; i++) out->bucket_refill_calls[i] = g_kheap_bucket_refills[i];
}
