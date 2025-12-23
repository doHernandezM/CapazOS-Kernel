//
//  memattr.h
//  OSpost
//
//  Stage-1 translation descriptor helpers (AArch64).
//  Phase 2: support 4KB pages so we can enforce RX/RO/NX and guard holes.
//

#pragma once
#include <stdint.h>

/* MAIR indices (AttrIndx in PTEs) */
enum {
    MAIR_IDX_DEVICE = 0,
    MAIR_IDX_NORMAL = 1,
};

/* MAIR attribute encodings (8-bit per index) */
#define MAIR_ATTR_DEVICE_nGnRE   0x04u  /* Device-nGnRE */
#define MAIR_ATTR_NORMAL_WBWA    0xFFu  /* Normal: Inner/Outer WB WA */

/* Build MAIR_EL1 value using the indices above. */
#define MAIR_VALUE  ( ((uint64_t)MAIR_ATTR_DEVICE_nGnRE) << (MAIR_IDX_DEVICE * 8) ) | \
                    ( ((uint64_t)MAIR_ATTR_NORMAL_WBWA)  << (MAIR_IDX_NORMAL * 8) )

/* Descriptor common bits */
#define PTE_VALID           (1ull << 0)

/* For L1/L2/L3:
   - Table descriptor: bits[1:0] = 0b11
   - L3 page descriptor: bits[1:0] = 0b11
*/
#define PTE_TABLE_OR_PAGE   (1ull << 1)
#define PTE_DESC_TABLE      (PTE_VALID | PTE_TABLE_OR_PAGE)
#define PTE_DESC_PAGE       (PTE_VALID | PTE_TABLE_OR_PAGE)

/* Access Flag */
#define PTE_AF              (1ull << 10)

/* Shareability (SH[9:8]) */
#define PTE_SH_NON          (0ull << 8)
#define PTE_SH_OUTER        (2ull << 8)
#define PTE_SH_INNER        (3ull << 8)

/* Access permissions (AP[7:6]) */
#define PTE_AP_RW_EL1       (0ull << 6)  /* EL1 RW, EL0 no access */
#define PTE_AP_RO_EL1       (2ull << 6)  /* EL1 RO, EL0 no access */

/* AttrIndx (bits[4:2]) */
#define PTE_ATTRINDX(x)     (((uint64_t)(x) & 0x7ull) << 2)

/* Execute-never (stage-1) */
#define PTE_PXN             (1ull << 53) /* Privileged execute never */
#define PTE_UXN             (1ull << 54) /* Unprivileged execute never */

/* Address fields */
#define PTE_TABLE_ADDR(pa)      ((uint64_t)(pa) & 0x0000FFFFFFFFF000ull)
#define PTE_PAGE_ADDR(pa)       ((uint64_t)(pa) & 0x0000FFFFFFFFF000ull)

/* Build a table descriptor pointing to a next-level table (4KB aligned). */
static inline uint64_t pte_table_desc(uint64_t next_table_pa) {
    return PTE_DESC_TABLE | PTE_TABLE_ADDR(next_table_pa);
}

/* Common builders for L3 pages */
static inline uint64_t pte_page_normal(uint64_t pa, uint64_t ap, uint64_t xn_bits) {
    return PTE_DESC_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | PTE_SH_INNER
         | ap
         | PTE_ATTRINDX(MAIR_IDX_NORMAL)
         | xn_bits;
}

static inline uint64_t pte_page_device(uint64_t pa, uint64_t ap, uint64_t xn_bits) {
    return PTE_DESC_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | PTE_SH_OUTER
         | ap
         | PTE_ATTRINDX(MAIR_IDX_DEVICE)
         | xn_bits;
}

/* Kernel policy helpers (Phase 2):
   - Kernel text: RX (RO + executable), UXN always set for kernel pages
   - Kernel rodata: RO + NX
   - Kernel data/stack: RW + NX
   - Device: RW + NX
*/
static inline uint64_t pte_ktext_rx(uint64_t pa) {
    /* Executable in EL1, never executable in EL0 */
    return pte_page_normal(pa, PTE_AP_RO_EL1, PTE_UXN /* PXN clear */);
}

static inline uint64_t pte_krodata_ro_nx(uint64_t pa) {
    return pte_page_normal(pa, PTE_AP_RO_EL1, PTE_PXN | PTE_UXN);
}

static inline uint64_t pte_kdata_rw_nx(uint64_t pa) {
    return pte_page_normal(pa, PTE_AP_RW_EL1, PTE_PXN | PTE_UXN);
}

static inline uint64_t pte_device_rw_nx(uint64_t pa) {
    return pte_page_device(pa, PTE_AP_RW_EL1, PTE_PXN | PTE_UXN);
}
