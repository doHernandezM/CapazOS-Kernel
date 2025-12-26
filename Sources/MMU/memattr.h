//
//  memattr.h
//  Capaz
//
//  Stage-1 translation descriptor helpers (AArch64).
//  Phase 3: used for TTBR1 (kernel) and TTBR0 (task) tables.
//

#pragma once
#include <stdint.h>

// MAIR indices (AttrIndx in PTEs)
enum {
    MAIR_IDX_DEVICE = 0,
    MAIR_IDX_NORMAL = 1,
};

// MAIR attribute encodings (8-bit per index)
#define MAIR_ATTR_DEVICE_nGnRE   0x04u  // Device-nGnRE
#define MAIR_ATTR_NORMAL_WBWA    0xFFu  // Normal: Inner/Outer Write-Back Write-Allocate

// Build MAIR_EL1 value using the indices above.
#define MAIR_VALUE  ( ((uint64_t)MAIR_ATTR_DEVICE_nGnRE) << (MAIR_IDX_DEVICE * 8) ) | \
                    ( ((uint64_t)MAIR_ATTR_NORMAL_WBWA)  << (MAIR_IDX_NORMAL * 8) )

// Descriptor bits (AArch64 stage-1)
#define PTE_VALID           (1ull << 0)
#define PTE_TABLE_OR_PAGE   (1ull << 1)   // 1 = table (L0-2) or page (L3)
#define PTE_TYPE_TABLE      (PTE_VALID | PTE_TABLE_OR_PAGE)   // bits[1:0] = 11
#define PTE_TYPE_PAGE       (PTE_VALID | PTE_TABLE_OR_PAGE)   // bits[1:0] = 11 (L3)

// Access Flag
#define PTE_AF              (1ull << 10)

// Shareability (SH[9:8])
#define PTE_SH_NON          (0ull << 8)
#define PTE_SH_OUTER        (2ull << 8)
#define PTE_SH_INNER        (3ull << 8)

// Access permissions (AP[7:6])
// 00: EL1 RW, EL0 no access
// (We intentionally keep EL0 permissions out of Phase 3 until the task model is real.)
#define PTE_AP_RW_EL1       (0ull << 6)
#define PTE_AP_RO_EL1       (2ull << 6)   // EL1 RO, EL0 no access

// AttrIndx (bits[4:2])
#define PTE_ATTRINDX(x)     (((uint64_t)(x) & 0x7ull) << 2)

// Execute-never (stage-1)
#define PTE_PXN             (1ull << 53)
#define PTE_UXN             (1ull << 54)

// Output address mask for 4KB pages (bits[47:12])
#define PTE_PAGE_ADDR(pa)   ((uint64_t)(pa) & 0x0000FFFFFFFFF000ull)

// Table address mask for next-level tables (bits[47:12])
#define PTE_TABLE_ADDR(pa)  ((uint64_t)(pa) & 0x0000FFFFFFFFF000ull)

static inline uint64_t pte_table_desc(uint64_t next_level_table_pa) {
    return PTE_TYPE_TABLE | PTE_TABLE_ADDR(next_level_table_pa);
}

static inline uint64_t pte_page_common(uint64_t pa, uint64_t ap, uint64_t sh, uint64_t attr, uint64_t xn_bits) {
    return PTE_TYPE_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | sh
         | ap
         | PTE_ATTRINDX(attr)
         | xn_bits;
}

// ---- Kernel policy constructors (TTBR1 mappings) ----

// Kernel .text: RX (no write), executable in EL1.
static inline uint64_t pte_ktext_rx(uint64_t pa) {
    return pte_page_common(pa, PTE_AP_RO_EL1, PTE_SH_INNER, MAIR_IDX_NORMAL, 0 /* executable */);
}

// Kernel .rodata: RO + NX
static inline uint64_t pte_krodata_ro_nx(uint64_t pa) {
    return pte_page_common(pa, PTE_AP_RO_EL1, PTE_SH_INNER, MAIR_IDX_NORMAL, PTE_PXN | PTE_UXN);
}

// Kernel data/bss/heap/stack: RW + NX
static inline uint64_t pte_kdata_rw_nx(uint64_t pa) {
    return pte_page_common(pa, PTE_AP_RW_EL1, PTE_SH_INNER, MAIR_IDX_NORMAL, PTE_PXN | PTE_UXN);
}

// Device MMIO: Device-nGnRE + NX
static inline uint64_t pte_device_rw_nx(uint64_t pa) {
    return pte_page_common(pa, PTE_AP_RW_EL1, PTE_SH_OUTER, MAIR_IDX_DEVICE, PTE_PXN | PTE_UXN);
}
