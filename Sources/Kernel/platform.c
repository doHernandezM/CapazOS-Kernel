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

static void sort_ranges(dtb_range_t *r, uint32_t n) {
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

bool platform_get_usable_ranges(const boot_info_t *boot_info,
                                dtb_range_t *out, uint32_t *inout_count)
{
    if (!out || !inout_count) return false;

    dtb_range_t mem[PLATFORM_MAX_RANGES];
    dtb_range_t rsv[PLATFORM_MAX_RANGES];
    uint32_t mem_n = PLATFORM_MAX_RANGES;
    uint32_t rsv_n = PLATFORM_MAX_RANGES;

    if (!dtb_get_memory_ranges(mem, &mem_n)) return false;
    if (!dtb_get_reserved_ranges(rsv, &rsv_n)) return false;

    /* Add implicit reservations: boot region, kernel image, DTB blob. */
    if (boot_info) {
        /* DTB blob itself (reserve *header totalsize*, not the mapped window size). */
        if (boot_info->dtb_ptr != 0 && rsv_n < PLATFORM_MAX_RANGES) {
            uint64_t dtb_pa = hh_virt_to_phys(boot_info->dtb_ptr);
            uint64_t dtb_sz = (uint64_t)dtb_get_totalsize();
            if (dtb_sz == 0) dtb_sz = boot_info->dtb_size; /* fallback */
            if (dtb_sz != 0) {
                rsv[rsv_n++] = (dtb_range_t){ .base = dtb_pa, .size = dtb_sz };
            }
        }

        /* Kernel image. */
        if (boot_info->kernel_size != 0 && rsv_n < PLATFORM_MAX_RANGES) {
            rsv[rsv_n++] = (dtb_range_t){ .base = boot_info->kernel_phys_base, .size = boot_info->kernel_size };
        }

        /* DTB blob itself (boot_info carries a VA in the direct map). */
        if (boot_info->dtb_ptr != 0 && boot_info->dtb_size != 0 && rsv_n < PLATFORM_MAX_RANGES) {
            uint64_t dtb_pa = hh_virt_to_phys(boot_info->dtb_ptr);
            rsv[rsv_n++] = (dtb_range_t){ .base = dtb_pa, .size = boot_info->dtb_size };
        }
    }

    /* Sort/merge reserved ranges for subtraction. */
    sort_ranges(rsv, rsv_n);
    rsv_n = normalize_merge(rsv, rsv_n);

    /* Sort/merge memory ranges too (for stable output). */
    sort_ranges(mem, mem_n);
    mem_n = normalize_merge(mem, mem_n);

    /* Subtract reserved ranges from memory ranges. */
    dtb_range_t usable[PLATFORM_MAX_RANGES];
    uint32_t usable_n = 0;

    for (uint32_t mi = 0; mi < mem_n; ++mi) {
        uint64_t cur = mem[mi].base;
        uint64_t end = range_end(mem[mi]);

        for (uint32_t ri = 0; ri < rsv_n; ++ri) {
            uint64_t rb = rsv[ri].base;
            uint64_t re = range_end(rsv[ri]);

            if (re <= cur) continue;
            if (rb >= end) break;

            if (rb > cur) {
                /* Emit [cur, rb) */
                if (usable_n < PLATFORM_MAX_RANGES) {
                    usable[usable_n++] = (dtb_range_t){ .base = cur, .size = rb - cur };
                }
            }

            /* Advance cur past this reserved range. */
            if (re > cur) cur = re;
            if (cur >= end) break;
        }

        if (cur < end) {
            if (usable_n < PLATFORM_MAX_RANGES) {
                usable[usable_n++] = (dtb_range_t){ .base = cur, .size = end - cur };
            }
        }
    }

    sort_ranges(usable, usable_n);
    usable_n = normalize_merge(usable, usable_n);

    /* Page-align usable ranges for a future PMM (4KiB pages). */
    for (uint32_t i = 0; i < usable_n; ++i) {
        uint64_t b = align_up_4k(usable[i].base);
        uint64_t e = align_down_4k(range_end(usable[i]));
        if (e <= b) {
            usable[i].size = 0; /* drop */
        } else {
            usable[i].base = b;
            usable[i].size = e - b;
        }
    }
    
    usable_n = normalize_merge(usable, usable_n);
    
    uint32_t cap = *inout_count;
    uint32_t n = (usable_n < cap) ? usable_n : cap;
    for (uint32_t i = 0; i < n; ++i) out[i] = usable[i];
    *inout_count = n;

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
