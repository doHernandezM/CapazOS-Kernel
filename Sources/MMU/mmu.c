//
//  mmu.c
//  OSpost
//
//  Created by Cosas on 12/21/25.
//

#include "mmu.h"

#include <stddef.h>
#include <stdint.h>

#include "memattr.h"
#include "linker_symbols.h"
#include "sysreg.h"

// You already provide memset in your project (mem.c or shims).
void *memset(void *dst, int c, size_t n);

// --- TCR_EL1 fields for: 4KB granule, 39-bit VA, TTBR0 only ---
// 39-bit VA => T0SZ = 64 - 39 = 25
// TG0 = 0b00 (4KB)
// SH0 = 0b11 (Inner shareable)
// IRGN0/ORGN0 = 0b01 (WBWA)
// IPS = 0b000 (32-bit PA). Mapping only covers first 2GB; sufficient for QEMU virt bring-up.

#define TCR_T0SZ(v)     ((uint64_t)(v) & 0x3Full)
#define TCR_IRGN0(v)    (((uint64_t)(v) & 0x3ull) << 8)
#define TCR_ORGN0(v)    (((uint64_t)(v) & 0x3ull) << 10)
#define TCR_SH0(v)      (((uint64_t)(v) & 0x3ull) << 12)
#define TCR_TG0(v)      (((uint64_t)(v) & 0x3ull) << 14)
#define TCR_IPS(v)      (((uint64_t)(v) & 0x7ull) << 32)

static inline uint64_t make_tcr_el1_for_39bit_4k(void) {
    const uint64_t t0sz  = TCR_T0SZ(25);
    const uint64_t irgn0 = TCR_IRGN0(1); // WBWA
    const uint64_t orgn0 = TCR_ORGN0(1); // WBWA
    const uint64_t sh0   = TCR_SH0(3);   // Inner shareable
    const uint64_t tg0   = TCR_TG0(0);   // 4KB
    const uint64_t ips   = TCR_IPS(0);   // 32-bit PA
    return t0sz | irgn0 | orgn0 | sh0 | tg0 | ips;
}

// Level-1 table for 39-bit VA, 4KB granule: 512 entries, each covers 1GB via block descriptor.
#define L1_ENTRIES 512

static inline size_t l1_index(uint64_t va) {
    return (size_t)((va >> 30) & 0x1FFull);
}

//void mmu_early_enable(void) {
//    /*
//     * Phase 3: All early MMU bring‑up is handled by mmu_bootstrap().
//     * Delegating here ensures that both kernel (TTBR1) and user
//     * (TTBR0) page tables are built and installed, and the stage‑1 MMU
//     * is enabled with write‑xor‑execute (WXN) and privilege access
//     * never (PAN) enabled.  Existing boot code can still call
//     * mmu_early_enable() without modification.
//     */
//    mmu_bootstrap();
//}

/*
 * Kernel‑global mappings init.  In Phase 3 the kernel runs in a higher‑half
 * virtual address space translated via TTBR1, while TTBR0 is reserved for
 * user spaces.  Calling mmu_bootstrap() here builds and installs both
 * page tables and enables the MMU.  This definition preserves the
 * mmu_kernel_init_global() symbol expected by older code.
 */
void mmu_kernel_init_global(void) {
    mmu_bootstrap();
}


void mmu_enable_caches(void) {
    uint64_t sctlr = read_sctlr_el1();

    // Enable D-cache + I-cache.
    sctlr |= (1ull << 2);    // C
    sctlr |= (1ull << 12);   // I

    // Keep the Phase 3 safety bits asserted (idempotent).
    sctlr |= (1ull << 19);   // WXN
    sctlr |= (1ull << 22);   // PAN

    write_sctlr_el1(sctlr);
    isb();
}
