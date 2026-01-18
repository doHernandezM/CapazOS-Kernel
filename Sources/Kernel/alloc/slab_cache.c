/*
 * slab_cache.c — simple slab/cache allocator (Milestone M5.5 Phase 1)
 */

#include "alloc/slab_cache.h"

#include "config.h"
#include "contracts.h"
#include "mm/mem.h"
#include "mm/pmm.h"

/*
 * This allocator uses 4KiB PMM pages as slabs.
 * Each slab page starts with a small header, followed by an array of fixed-size objects.
 * Freed objects store a next pointer in their first word (intrusive free list).
 */

#define SLAB_PAGE_SIZE 4096u

/* Poison pattern for freed slab objects (helps catch UAF). */
#ifndef CONFIG_POISON_SLAB_FREE
#define CONFIG_POISON_SLAB_FREE 1
#endif

typedef struct slab_page {
    struct slab_page *next;
    void *freelist;
    uint16_t obj_count;
    uint16_t inuse;
    /* object region starts after this header */
} slab_page_t;

static inline uint32_t u32_max(uint32_t a, uint32_t b) { return a > b ? a : b; }

static inline uintptr_t align_up(uintptr_t v, uintptr_t a) {
    if (a == 0) return v;
    return (v + (a - 1)) & ~(a - 1);
}

static void slab_page_build_freelist(slab_page_t *sp, uint32_t obj_size, uint32_t obj_align) {
    uintptr_t base = (uintptr_t)sp;
    uintptr_t cursor = align_up(base + sizeof(*sp), obj_align);
    uintptr_t end = base + SLAB_PAGE_SIZE;

    uint16_t count = 0;
    void *head = NULL;

    while (cursor + obj_size <= end) {
        void *obj = (void *)cursor;
        /* push-front */
        *(void **)obj = head;
        head = obj;
        cursor += obj_size;
        count++;
    }

    sp->freelist = head;
    sp->obj_count = count;
    sp->inuse = 0;

    if (count == 0) {
        panic("slab: zero capacity");
    }
}

static slab_page_t *slab_page_alloc(uint32_t obj_size, uint32_t obj_align) {
    uint64_t pa = 0;
    if (!pmm_alloc_pages(1, &pa)) {
        return NULL;
    }

    slab_page_t *sp = (slab_page_t *)(uintptr_t)pmm_phys_to_virt(pa);
    sp->next = NULL;
    sp->freelist = NULL;
    sp->obj_count = 0;
    sp->inuse = 0;

    slab_page_build_freelist(sp, obj_size, obj_align);
    return sp;
}

void slab_cache_init(slab_cache_t *c, const char *name, size_t obj_size, size_t align) {
    if (!c) {
        panic("slab_cache_init: null");
    }
    if (obj_size == 0) {
        panic("slab_cache_init: obj_size=0");
    }

    /* Ensure we can store the intrusive next pointer. */
    uint32_t want_align = (uint32_t)u32_max((uint32_t)align, (uint32_t)sizeof(void *));
    if ((want_align & (want_align - 1)) != 0) {
        /* must be power-of-two alignment */
        panic("slab_cache_init: bad align");
    }

    uint32_t sz = (uint32_t)obj_size;
    sz = (uint32_t)align_up(sz, want_align);
    if (sz < (uint32_t)sizeof(void *)) {
        sz = (uint32_t)sizeof(void *);
    }

    /* Ensure header + at least one object fits in a single 4KiB slab page. */
    uintptr_t first = align_up((uintptr_t)0 + sizeof(slab_page_t), want_align);
    if ((first + sz) > SLAB_PAGE_SIZE) {
        panic("slab_cache_init: obj too large");
    }

    c->name = name ? name : "slab";
    c->obj_size = sz;
    c->obj_align = want_align;
    c->pages = NULL;

    /* Stats (best-effort, always-on for now). */
    c->alloc_calls = 0;
    c->free_calls = 0;
    c->inuse_objects = 0;
    c->peak_inuse_objects = 0;
    c->slab_pages_allocated = 0;
    c->alloc_failures = 0;
}

void *slab_alloc(slab_cache_t *c) {
    ASSERT_THREAD_CONTEXT();
    if (!c) {
        panic("slab_alloc: null cache");
    }

    c->alloc_calls++;

    /* Find a page with free objects. */
    for (slab_page_t *sp = c->pages; sp; sp = sp->next) {
        if (sp->freelist) {
            void *obj = sp->freelist;
            sp->freelist = *(void **)obj;
            sp->inuse++;

            c->inuse_objects++;
            if (c->inuse_objects > c->peak_inuse_objects) {
                c->peak_inuse_objects = c->inuse_objects;
            }
            return obj;
        }
    }

    /* Allocate a new slab page from PMM and add it to the cache. */
    slab_page_t *sp = slab_page_alloc(c->obj_size, c->obj_align);
    if (!sp) {
        c->alloc_failures++;
        return NULL;
    }
    c->slab_pages_allocated++;
    sp->next = c->pages;
    c->pages = sp;

    void *obj = sp->freelist;
    sp->freelist = *(void **)obj;
    sp->inuse++;

    c->inuse_objects++;
    if (c->inuse_objects > c->peak_inuse_objects) {
        c->peak_inuse_objects = c->inuse_objects;
    }
    return obj;
}

void slab_free(slab_cache_t *c, void *p) {
    ASSERT_THREAD_CONTEXT();
    if (!c || !p) {
        return;
    }

    c->free_calls++;

    uintptr_t page_base = (uintptr_t)p & ~(uintptr_t)(SLAB_PAGE_SIZE - 1);
    slab_page_t *sp = (slab_page_t *)page_base;

    /* Validate that the page belongs to this cache. */
    bool found = false;
    for (slab_page_t *it = c->pages; it; it = it->next) {
        if (it == sp) {
            found = true;
            break;
        }
    }
    if (!found) {
        panic("slab_free: foreign ptr");
    }
    if (sp->inuse == 0) {
        panic("slab_free: underflow");
    }

    /* Push object back to freelist (intrusive). */
#if CONFIG_POISON_SLAB_FREE
    memset(p, 0xA5, c->obj_size);
#endif
    *(void **)p = sp->freelist;
    sp->freelist = p;
    sp->inuse--;

    if (c->inuse_objects == 0) {
        panic("slab_free: cache underflow");
    }
    c->inuse_objects--;
}

bool slab_cache_get_stats(const slab_cache_t *c, slab_cache_stats_t *out) {
    if (!c || !out) {
        return false;
    }
    out->alloc_calls = c->alloc_calls;
    out->free_calls = c->free_calls;
    out->inuse_objects = c->inuse_objects;
    out->peak_inuse_objects = c->peak_inuse_objects;
    out->slab_pages_allocated = c->slab_pages_allocated;
    out->alloc_failures = c->alloc_failures;
    return true;
}
