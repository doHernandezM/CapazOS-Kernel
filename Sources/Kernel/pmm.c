#include "pmm.h"

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "platform.h"
#include "dtb.h"
#include "uart_pl011.h"

/* Must match platform.c + mmu.c direct-map assumptions. */
#define PAGE_SIZE 0x1000ULL
#define RAM_BASE 0x40000000ULL
#define RAM_DIRECTMAP_SIZE 0x40000000ULL /* 1 GiB */
#define HH_PHYS_4000_BASE 0xFFFF800040000000ULL

/* Option B: fixed metadata reservation immediately after kernel runtime end. */
#define PMM_METADATA_PAGES 16ULL

/* Linker symbol: page-aligned end of the kernel runtime footprint (.bss included). */
extern uint8_t __kernel_runtime_end[];

/* freestanding libc */
void *memset(void *dst, int c, size_t n);

typedef struct pmm_state {
    uint64_t base_pa;        /* inclusive */
    uint64_t limit_pa;       /* exclusive */
    uint64_t total_pages;
    uint64_t free_pages;

    uint8_t *bitmap;         /* VA (high-half direct map) */
    uint64_t bitmap_bytes;

    uint64_t next_hint;      /* page index hint */
    uint64_t meta_base_pa;
    uint64_t meta_pages;
} pmm_state_t;

/* Stored in the metadata region; also cached as a VA pointer. */
static pmm_state_t *g_pmm = 0;

static inline uint64_t align_down_4k(uint64_t x) { return x & ~(PAGE_SIZE - 1ULL); }
static inline uint64_t align_up_4k(uint64_t x)   { return (x + (PAGE_SIZE - 1ULL)) & ~(PAGE_SIZE - 1ULL); }

static inline uint64_t hh_virt_to_phys(uint64_t va) {
    if (va >= HH_PHYS_4000_BASE) {
        return (va - HH_PHYS_4000_BASE) + RAM_BASE;
    }
    return va;
}

static inline uint64_t phys_to_hh_virt(uint64_t pa) {
    return HH_PHYS_4000_BASE + (pa - RAM_BASE);
}

static inline void bit_set(uint8_t *bm, uint64_t idx) {
    bm[idx >> 3] |= (uint8_t)(1u << (idx & 7));
}

static inline void bit_clear(uint8_t *bm, uint64_t idx) {
    bm[idx >> 3] &= (uint8_t)~(1u << (idx & 7));
}

static inline bool bit_test(const uint8_t *bm, uint64_t idx) {
    return ((bm[idx >> 3] >> (idx & 7)) & 1u) != 0;
}

static void pmm_panic(const char *msg) {
    uart_puts("PMM PANIC: ");
    uart_puts(msg);
    uart_puts("\n");
    for (;;) {
        __asm__ volatile("wfe");
    }
}

/* Mark all pages reserved (1). */
static void bitmap_mark_all_reserved(pmm_state_t *st) {
    memset(st->bitmap, 0xFF, (size_t)st->bitmap_bytes);
}

/* Mark a physical range [start,end) free (0), clamped to the PMM window. */
static void bitmap_mark_range_free(pmm_state_t *st, uint64_t start_pa, uint64_t end_pa) {
    if (end_pa <= start_pa) return;

    if (start_pa < st->base_pa) start_pa = st->base_pa;
    if (end_pa   > st->limit_pa) end_pa = st->limit_pa;

    start_pa = align_up_4k(start_pa);
    end_pa   = align_down_4k(end_pa);
    if (end_pa <= start_pa) return;

    for (uint64_t pa = start_pa; pa < end_pa; pa += PAGE_SIZE) {
        uint64_t idx = (pa - st->base_pa) / PAGE_SIZE;
        if (idx >= st->total_pages) break;
        if (bit_test(st->bitmap, idx)) {
            bit_clear(st->bitmap, idx);
            st->free_pages++;
        }
    }
}

/* Mark a physical range [start,end) reserved (1), clamped to the PMM window. */
static void bitmap_mark_range_reserved(pmm_state_t *st, uint64_t start_pa, uint64_t end_pa) {
    if (end_pa <= start_pa) return;

    if (start_pa < st->base_pa) start_pa = st->base_pa;
    if (end_pa   > st->limit_pa) end_pa = st->limit_pa;

    start_pa = align_down_4k(start_pa);
    end_pa   = align_up_4k(end_pa);
    if (end_pa <= start_pa) return;

    for (uint64_t pa = start_pa; pa < end_pa; pa += PAGE_SIZE) {
        uint64_t idx = (pa - st->base_pa) / PAGE_SIZE;
        if (idx >= st->total_pages) break;
        if (!bit_test(st->bitmap, idx)) {
            bit_set(st->bitmap, idx);
            if (st->free_pages) st->free_pages--;
        }
    }
}

/* Compute the max end of clamped DTB memory ranges, or fall back to RAM_DIRECTMAP_SIZE. */
static uint64_t compute_limit_from_dtb(void) {
    dtb_range_t mem[64];
    uint32_t mem_n = (uint32_t)(sizeof(mem) / sizeof(mem[0]));
    if (!dtb_get_memory_ranges(mem, &mem_n) || mem_n == 0) {
        return RAM_BASE + RAM_DIRECTMAP_SIZE;
    }

    uint64_t max_end = RAM_BASE;
    for (uint32_t i = 0; i < mem_n; i++) {
        uint64_t start = mem[i].base;
        uint64_t end = mem[i].base + mem[i].size;
        /* Clamp to our direct-map window. */
        if (end <= RAM_BASE) continue;
        if (start < RAM_BASE) start = RAM_BASE;
        uint64_t window_end = RAM_BASE + RAM_DIRECTMAP_SIZE;
        if (start >= window_end) continue;
        if (end > window_end) end = window_end;
        if (end > max_end) max_end = end;
    }

    if (max_end <= RAM_BASE) {
        max_end = RAM_BASE + RAM_DIRECTMAP_SIZE;
    }
    return max_end;
}

void pmm_init(const boot_info_t *boot_info) {
    if (!boot_info) {
        pmm_panic("boot_info is NULL");
    }

    /* Query PMM-usable ranges from the platform layer (already page-aligned, clamped, non-overlapping). */
    dtb_range_t usable[64];
    uint32_t usable_n = (uint32_t)(sizeof(usable) / sizeof(usable[0]));
    if (!platform_get_usable_ranges(boot_info, usable, &usable_n) || usable_n == 0) {
        pmm_panic("platform_get_usable_ranges failed or returned empty");
    }

    /* Determine managed window: base_pa = min(usable), limit_pa = max_end(usable). */
    uint64_t base_pa = 0;
    uint64_t limit_pa = 0;
    for (uint32_t i = 0; i < usable_n; i++) {
        uint64_t start = align_up_4k(usable[i].base);
        uint64_t end = align_down_4k(usable[i].base + usable[i].size);
        if (end <= start) continue;
        if (base_pa == 0 || start < base_pa) base_pa = start;
        if (end > limit_pa) limit_pa = end;
    }

    if (base_pa == 0 || limit_pa <= base_pa) {
        pmm_panic("invalid PMM window from usable ranges");
    }

    /* Clamp to our TTBR1 direct-map window for short-term correctness. */
    const uint64_t win_start = RAM_BASE;
    const uint64_t win_end = RAM_BASE + RAM_DIRECTMAP_SIZE;
    if (base_pa < win_start) base_pa = win_start;
    if (limit_pa > win_end) limit_pa = win_end;
    base_pa = align_up_4k(base_pa);
    limit_pa = align_down_4k(limit_pa);

    if (limit_pa <= base_pa) {
        pmm_panic("PMM window empty after clamp");
    }

    /* Metadata region: fixed reservation immediately after kernel runtime end. */
    uint64_t runtime_end_pa = hh_virt_to_phys((uint64_t)__kernel_runtime_end);
    uint64_t meta_base_pa = align_up_4k(runtime_end_pa);
    uint64_t meta_bytes = PMM_METADATA_PAGES * PAGE_SIZE;
    uint64_t meta_end_pa = meta_base_pa + meta_bytes;

    /* Metadata must live in the mapped direct-map window (TTBR1). */
    if (meta_base_pa < win_start || meta_end_pa > win_end) {
        pmm_panic("metadata region outside mapped RAM window");
    }

    uint64_t meta_base_va = phys_to_hh_virt(meta_base_pa);

    /* Place state then bitmap within the metadata region. */
    pmm_state_t *st = (pmm_state_t *)(uintptr_t)meta_base_va;

    st->base_pa = base_pa;
    st->limit_pa = limit_pa;
    st->total_pages = (st->limit_pa - st->base_pa) / PAGE_SIZE;
    st->free_pages = 0;
    st->next_hint = 0;
    st->meta_base_pa = meta_base_pa;
    st->meta_pages = PMM_METADATA_PAGES;

    uint64_t bitmap_bytes = (st->total_pages + 7ULL) / 8ULL;
    uint64_t state_bytes = (uint64_t)sizeof(pmm_state_t);
    uint64_t bitmap_off = (state_bytes + 7ULL) & ~7ULL;
    if (bitmap_off + bitmap_bytes > meta_bytes) {
        pmm_panic("metadata pages insufficient for bitmap");
    }

    st->bitmap = (uint8_t *)(uintptr_t)(meta_base_va + bitmap_off);
    st->bitmap_bytes = bitmap_bytes;

    /* Initialize: everything reserved, then clear bits for usable ranges. */
    bitmap_mark_all_reserved(st);
    for (uint32_t i = 0; i < usable_n; i++) {
        uint64_t start = usable[i].base;
        uint64_t end = usable[i].base + usable[i].size;
        bitmap_mark_range_free(st, start, end);
    }

    /*
     * Defensive reservation pass (even though platform_get_usable_ranges should already subtract these).
     * Subtract implicit reservations:
     *  - boot region [lowest_ram_base, kernel_phys_base)
     *  - kernel runtime footprint [kernel_phys_base, runtime_end_pa)
     *  - DTB blob range
     *  - PMM metadata pages
     */
    /* Boot region: derive lowest RAM base from DTB memory ranges (clamped). */
    {
        dtb_range_t mem[64];
        uint32_t mem_n = (uint32_t)(sizeof(mem) / sizeof(mem[0]));
        uint64_t lowest = win_end;
        if (dtb_get_memory_ranges(mem, &mem_n) && mem_n != 0) {
            for (uint32_t i = 0; i < mem_n; i++) {
                uint64_t start = mem[i].base;
                uint64_t end = mem[i].base + mem[i].size;
                if (end <= win_start) continue;
                if (start < win_start) start = win_start;
                if (start >= win_end) continue;
                if (end > win_end) end = win_end;
                start = align_up_4k(start);
                end = align_down_4k(end);
                if (end <= start) continue;
                if (start < lowest) lowest = start;
            }
        }
        if (lowest == win_end) lowest = win_start;
        bitmap_mark_range_reserved(st, lowest, boot_info->kernel_phys_base);
    }

    /* Kernel runtime footprint. */
    bitmap_mark_range_reserved(
        st,
        boot_info->kernel_phys_base,
        align_up_4k(runtime_end_pa)
    );

    /* DTB blob range (boot_info->dtb_ptr is a high-half VA). */
    if (boot_info->dtb_ptr != 0) {
        uint64_t dtb_phys = hh_virt_to_phys(boot_info->dtb_ptr);
        uint64_t dtb_sz = dtb_get_totalsize();
        if (dtb_sz == 0) dtb_sz = boot_info->dtb_size;
        if (boot_info->dtb_size && dtb_sz > boot_info->dtb_size) dtb_sz = boot_info->dtb_size;
        if (dtb_sz) {
            bitmap_mark_range_reserved(
                st,
                align_down_4k(dtb_phys),
                align_up_4k(dtb_phys + dtb_sz)
            );
        }
    }

    /* PMM metadata pages. */
    bitmap_mark_range_reserved(st, meta_base_pa, meta_end_pa);

    g_pmm = st;
    pmm_dump_summary();
}

bool pmm_alloc_pages(uint32_t count, uint64_t *out_pa) {
    pmm_state_t *st = g_pmm;
    if (!st || !out_pa || count == 0) return false;
    if (st->free_pages < (uint64_t)count) return false;

    uint64_t n = st->total_pages;
    if ((uint64_t)count > n) return false;

    uint64_t start = st->next_hint;
    if (start >= n) start = 0;

    for (uint64_t pass = 0; pass < 2; pass++) {
        uint64_t i0 = (pass == 0) ? start : 0;
        uint64_t i1 = (pass == 0) ? n : start;

        if (i1 < (uint64_t)count) continue;

        for (uint64_t i = i0; i + (uint64_t)count <= i1; i++) {
            /* Check for a run of `count` free bits. */
            bool ok = true;
            for (uint32_t j = 0; j < count; j++) {
                if (bit_test(st->bitmap, i + (uint64_t)j)) { ok = false; break; }
            }
            if (!ok) continue;

            /* Commit: mark all bits reserved. */
            for (uint32_t j = 0; j < count; j++) {
                bit_set(st->bitmap, i + (uint64_t)j);
            }

            st->free_pages -= (uint64_t)count;
            st->next_hint = i + (uint64_t)count;
            *out_pa = st->base_pa + (i * PAGE_SIZE);
            return true;
        }
    }

    return false;
}

bool pmm_alloc_page(uint64_t *out_pa) {
    return pmm_alloc_pages(1, out_pa);
}

void *pmm_alloc_page_va(uint64_t *out_pa) {
    uint64_t pa = 0;
    if (!pmm_alloc_page(&pa)) return (void *)0;
    if (out_pa) *out_pa = pa;
    return (void *)(uintptr_t)phys_to_hh_virt(pa);
}

void pmm_free_page(uint64_t pa) {
    pmm_state_t *st = g_pmm;
    if (!st) return;

    if (pa < st->base_pa || pa >= st->limit_pa) {
        return;
    }
    if ((pa & (PAGE_SIZE - 1ULL)) != 0) {
        return;
    }

    uint64_t idx = (pa - st->base_pa) / PAGE_SIZE;
    if (idx >= st->total_pages) return;

    /* Do not allow freeing metadata pages. */
    if (pa >= st->meta_base_pa && pa < (st->meta_base_pa + st->meta_pages * PAGE_SIZE)) {
        return;
    }

    if (bit_test(st->bitmap, idx)) {
        bit_clear(st->bitmap, idx);
        st->free_pages++;
        if (idx < st->next_hint) st->next_hint = idx;
    }
}

void pmm_dump_summary(void) {
    pmm_state_t *st = g_pmm;
    if (!st) {
        uart_puts("PMM: <uninitialized>\n");
        return;
    }

    /* Always emit a stable, human-readable summary; add details under KMAIN_DEBUG. */
    uart_puts("PMM(free/total): ");
    uart_putu64_dec(st->free_pages);
    uart_putc('/');
    uart_putu64_dec(st->total_pages);
    uart_puts("\n");

#if KMAIN_DEBUG
    uart_puts("PMM: base_pa="); uart_puthex64(st->base_pa);
    uart_puts(" limit_pa="); uart_puthex64(st->limit_pa);
    uart_puts(" total_pages="); uart_puthex64(st->total_pages);
    uart_puts(" free_pages="); uart_puthex64(st->free_pages);
    uart_puts(" meta_pa="); uart_puthex64(st->meta_base_pa);
    uart_puts(" meta_pages="); uart_puthex64(st->meta_pages);
    uart_puts(" bitmap_bytes="); uart_puthex64(st->bitmap_bytes);
    uart_puts("\n");
#endif
}
