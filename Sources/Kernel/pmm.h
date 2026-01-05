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
 * - PMM state + bitmap are stored in a fixed metadata region placed
 *   immediately after the kernel runtime footprint.
 *
 * IMPORTANT: TTBR0 is disabled; all PMM metadata and all allocated pages
 * must be reachable via TTBR1 high-half direct map.
 */

void pmm_init(const boot_info_t *bi);

bool pmm_is_initialized(void);

/* Allocate a single 4KiB physical page. */
bool pmm_alloc_page(uint64_t *out_pa);

/* Allocate a contiguous run of count 4KiB pages. */
bool pmm_alloc_pages(uint32_t count, uint64_t *out_pa);

/* Free a previously allocated page (must be within PMM window). */
void pmm_free_page(uint64_t pa);

/* Optional: allocate one page and return its direct-mapped VA. */
void *pmm_alloc_page_va(uint64_t *out_pa);

/* Debug: print PMM summary to UART. */
void pmm_dump_summary(void);

/*
 * Direct-map helpers for the QEMU virt baseline.
 * Must match mmu.c and boot mapping.
 */
static inline uint64_t pmm_phys_to_virt(uint64_t pa) {
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
