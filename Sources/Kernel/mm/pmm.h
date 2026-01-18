#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>

#include "boot_info.h"

/*
 * Bitmap Physical Memory Manager (PMM)
 *
 * - 4KiB pages
 * - bitmap bit = 1 => allocated/reserved, 0 => free
 * - bitmap and PMM state are stored in a fixed metadata region placed
 *   immediately after the kernel runtime footprint.
 *
 * IMPORTANT: TTBR0 is disabled; all PMM metadata must be reachable via
 * TTBR1 high-half direct map.
 */

void pmm_init(const boot_info_t *bi);

/* Allocate a single 4KiB physical page. Returns true on success. */
bool pmm_alloc_page(uint64_t *out_pa);

/* Allocate `count` contiguous 4KiB physical pages. Returns true on success. */
bool pmm_alloc_pages(uint32_t count, uint64_t *out_pa);

/* Free a previously allocated page (must be within PMM window). */
void pmm_free_page(uint64_t pa);

/* Optional: allocate a page and return its high-half direct-mapped VA (NULL on OOM). */
void *pmm_alloc_page_va(uint64_t *out_pa);

/* Debug: print PMM summary to UART. */
void pmm_dump_summary(void);

/* Query basic PMM counters (returns false if PMM not initialized). */
bool pmm_get_stats(uint64_t *out_free_pages, uint64_t *out_total_pages);

/* Extended PMM stats for hardening (Milestone M4.5). */
typedef struct pmm_stats_ex {
    uint64_t free_pages;
    uint64_t total_pages;
    uint64_t low_free_pages_seen;  /* low-water mark */
    uint64_t peak_used_pages_seen; /* high-water mark */
    uint64_t alloc_pages_calls;
    uint64_t alloc_contig_calls;
    uint64_t free_page_calls;
} pmm_stats_ex_t;

bool pmm_get_stats_ex(pmm_stats_ex_t *out);

/* Direct-map helpers for the QEMU virt baseline. */
static inline uint64_t pmm_phys_to_virt(uint64_t pa) {
    /* Must match mmu.c and boot mapping. */
    const uint64_t RAM_BASE = 0x40000000ULL;
    const uint64_t HH_PHYS_4000_BASE = 0xFFFF800040000000ULL;
    return HH_PHYS_4000_BASE + (pa - RAM_BASE);
}

static inline uint64_t pmm_virt_to_phys(uint64_t va) {
    const uint64_t RAM_BASE = 0x40000000ULL;
    const uint64_t HH_PHYS_4000_BASE = 0xFFFF800040000000ULL;
    if (va >= HH_PHYS_4000_BASE) {
        return (va - HH_PHYS_4000_BASE) + RAM_BASE;
    }
    return va;
}

#endif /* PMM_H */
