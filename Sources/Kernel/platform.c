#include "platform.h"

#include <stddef.h>

#include "uart_pl011.h"

#define PLATFORM_MAX_RANGES  64

/* QEMU virt baseline RAM starts at 0x4000_0000. */
#define RAM_BASE 0x40000000ULL

/* High-half direct map base for RAM_BASE. Must match boot + mmu.c. */
#define HH_PHYS_4000_BASE 0xFFFF800040000000ULL

static inline uint64_t hh_virt_to_phys(uint64_t va) {
    if (va >= HH_PHYS_4000_BASE) {
        return (va - HH_PHYS_4000_BASE) + RAM_BASE;
    }
    return va;
}

static inline uint64_t range_end(dtb_range_t r) {
    return r.base + r.size;
}

#define PAGE_SIZE 0x1000ULL
static inline uint64_t align_up_4k(uint64_t x) {
    return (x + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}
static inline uint64_t align_down_4k(uint64_t x) {
    return x & ~(PAGE_SIZE - 1);
}

static void __attribute__((unused)) sort_ranges(dtb_range_t *r, uint32_t n) {
    /* Selection sort (small N, no libc). */
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t min = i;
        for (uint32_t j = i + 1; j < n; ++j) {
            if (r[j].base < r[min].base) min = j;
        }
        if (min != i) {
            dtb_range_t tmp = r[i];
            r[i] = r[min];
            r[min] = tmp;
        }
    }
}

static uint32_t normalize_merge(dtb_range_t *r, uint32_t n) {
    /* Remove zero-sized and merge overlaps/adjacent. Assumes sorted by base. */
    uint32_t w = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (r[i].size == 0) continue;
        if (w == 0) {
            r[w++] = r[i];
            continue;
        }
        dtb_range_t *prev = &r[w - 1];
        uint64_t prev_end = range_end(*prev);
        uint64_t cur_end  = range_end(r[i]);
        if (r[i].base <= prev_end) {
            /* overlap/adjacent */
            if (cur_end > prev_end) {
                prev->size = cur_end - prev->base;
            }
        } else {
            r[w++] = r[i];
        }
    }
    return w;
}

static bool subtract_reserved(const dtb_range_t mem[], uint32_t mem_n,
                             const dtb_range_t rsv[], uint32_t rsv_n,
                             dtb_range_t out[], uint32_t *inout_n) {
    if (!out || !inout_n) return false;
    uint32_t cap = *inout_n;
    uint32_t out_n = 0;

    uint32_t j = 0;
    for (uint32_t i = 0; i < mem_n; ++i) {
        if (mem[i].size == 0) continue;
        uint64_t m_start = mem[i].base;
        uint64_t m_end = range_end(mem[i]);

        /* Advance reserved cursor to the first range that might overlap. */
        while (j < rsv_n && range_end(rsv[j]) <= m_start) j++;

        uint64_t cur = m_start;
        for (uint32_t k = j; k < rsv_n && rsv[k].base < m_end; ++k) {
            uint64_t r_start = rsv[k].base;
            uint64_t r_end = range_end(rsv[k]);

            if (r_start > cur) {
                uint64_t seg_end = (r_start < m_end) ? r_start : m_end;
                if (seg_end > cur) {
                    if (out_n >= cap) return false;
                    out[out_n++] = (dtb_range_t){ .base = cur, .size = seg_end - cur };
                }
            }

            if (r_end > cur) cur = r_end;
            if (cur >= m_end) break;
        }

        if (cur < m_end) {
            if (out_n >= cap) return false;
            out[out_n++] = (dtb_range_t){ .base = cur, .size = m_end - cur };
        }
    }

    *inout_n = out_n;
    return true;
}

static void print_ranges(const char *title, const dtb_range_t *r, uint32_t n) {
    uart_puts(title);
    if (n == 0) {
        uart_puts(" <none>\n");
        return;
    }
    uart_putnl();
    for (uint32_t i = 0; i < n; ++i) {
        uart_puts("  ["); uart_puthex64(i); uart_puts("] base=");
        uart_puthex64(r[i].base);
        uart_puts(" size="); uart_puthex64(r[i].size);
        uart_puts(" end="); uart_puthex64(range_end(r[i]));
        uart_putnl();
    }
}

bool platform_get_usable_ranges(const boot_info_t *boot_info, dtb_range_t out[], uint32_t *inout_count) {
    if (!out || !inout_count) return false;

    dtb_range_t mem[PLATFORM_MAX_RANGES];
    dtb_range_t rsv[PLATFORM_MAX_RANGES];
    uint32_t mem_n = PLATFORM_MAX_RANGES;
    uint32_t rsv_n = PLATFORM_MAX_RANGES;

    if (!dtb_get_memory_ranges(mem, &mem_n)) return false;
    if (!dtb_get_reserved_ranges(rsv, &rsv_n)) return false;

    // Determine lowest RAM base from DTB memory nodes.
    uint64_t mem_min_base = UINT64_MAX;
    for (uint32_t i = 0; i < mem_n; i++) {
        if (mem[i].size == 0) continue;
        if (mem[i].base < mem_min_base) mem_min_base = mem[i].base;
    }

    // Normalize DTB-provided RAM spans to whole pages.
    uint32_t mem_w = 0;
    for (uint32_t i = 0; i < mem_n; i++) {
        uint64_t start = align_up_4k(mem[i].base);
        uint64_t end = align_down_4k(mem[i].base + mem[i].size);
        if (end <= start) continue;
        mem[mem_w].base = start;
        mem[mem_w].size = end - start;
        mem_w++;
    }
    mem_n = mem_w;

    // Build combined reserved list: DTB-provided + implicit.
    dtb_range_t all_rsv[PLATFORM_MAX_RANGES * 2];
    uint32_t all_rsv_n = 0;

    // DTB-provided reserved ranges: cover whole pages.
    for (uint32_t i = 0; i < rsv_n && all_rsv_n < (uint32_t)(sizeof(all_rsv) / sizeof(all_rsv[0])); i++) {
        if (rsv[i].size == 0) continue;
        uint64_t start = align_down_4k(rsv[i].base);
        uint64_t end = align_up_4k(rsv[i].base + rsv[i].size);
        if (end <= start) continue;
        all_rsv[all_rsv_n++] = (dtb_range_t){ .base = start, .size = end - start };
    }

    // Implicit reservations: boot region, kernel image, DTB blob.
    if (boot_info) {
        // Reserve early boot region below the kernel load.
        if (mem_min_base != UINT64_MAX && boot_info->kernel_phys_base > mem_min_base) {
            uint64_t start = align_down_4k(mem_min_base);
            uint64_t end = align_up_4k(boot_info->kernel_phys_base);
            if (end > start && all_rsv_n < (uint32_t)(sizeof(all_rsv) / sizeof(all_rsv[0]))) {
                all_rsv[all_rsv_n++] = (dtb_range_t){ .base = start, .size = end - start };
            }
        }

        // Reserve the kernel runtime footprint (through .bss).
        {
            uint64_t start = align_down_4k(boot_info->kernel_phys_base);
            uint64_t end = align_up_4k(boot_info->kernel_phys_base + boot_info->kernel_runtime_size);
            if (end > start && all_rsv_n < (uint32_t)(sizeof(all_rsv) / sizeof(all_rsv[0]))) {
                all_rsv[all_rsv_n++] = (dtb_range_t){ .base = start, .size = end - start };
            }
        }

        // Reserve the DTB blob at its physical address.
        {
            uint64_t dtb_phys = hh_virt_to_phys((uint64_t)boot_info->dtb_ptr);
            uint64_t dtb_sz = dtb_get_totalsize();
            if (dtb_sz == 0) dtb_sz = boot_info->dtb_size;
            if (boot_info->dtb_size && dtb_sz > boot_info->dtb_size) dtb_sz = boot_info->dtb_size;

            uint64_t start = align_down_4k(dtb_phys);
            uint64_t end = align_up_4k(dtb_phys + dtb_sz);
            if (end > start && all_rsv_n < (uint32_t)(sizeof(all_rsv) / sizeof(all_rsv[0]))) {
                all_rsv[all_rsv_n++] = (dtb_range_t){ .base = start, .size = end - start };
            }
        }
    }

    all_rsv_n = normalize_merge(all_rsv, all_rsv_n);

    // Subtract reserved from memory.
    if (!subtract_reserved(mem, mem_n, all_rsv, all_rsv_n, out, inout_count)) return false;

    // Ensure output is page-aligned and non-empty.
    uint32_t out_n = *inout_count;
    uint32_t out_w = 0;
    for (uint32_t i = 0; i < out_n; i++) {
        uint64_t start = align_up_4k(out[i].base);
        uint64_t end = align_down_4k(out[i].base + out[i].size);
        if (end <= start) continue;
        out[out_w].base = start;
        out[out_w].size = end - start;
        out_w++;
    }
    *inout_count = out_w;

    return true;
}

void platform_dump_memory_map(const boot_info_t *boot_info) {
    dtb_range_t mem[PLATFORM_MAX_RANGES];
    dtb_range_t rsv[PLATFORM_MAX_RANGES];
    dtb_range_t usable[PLATFORM_MAX_RANGES];
    uint32_t mem_n = PLATFORM_MAX_RANGES;
    uint32_t rsv_n = PLATFORM_MAX_RANGES;
    uint32_t usable_n = PLATFORM_MAX_RANGES;

    (void)boot_info;

    if (!dtb_get_memory_ranges(mem, &mem_n)) mem_n = 0;
    if (!dtb_get_reserved_ranges(rsv, &rsv_n)) rsv_n = 0;

    print_ranges("DTB: memory ranges:", mem, mem_n);
    print_ranges("DTB: reserved ranges (DTB-provided):", rsv, rsv_n);

    if (platform_get_usable_ranges(boot_info, usable, &usable_n)) {
        print_ranges("PLAT: usable ranges:", usable, usable_n);
    } else {
        uart_puts("PLAT: usable ranges: <unavailable>\n");
    }
}
