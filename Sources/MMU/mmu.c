//
//  mmu.c
//  OSpost
//
//  Phase 2: permission-shaped mappings with 4KB pages.
//  - VA==PA (identity) to minimize churn
//  - .text:   RX
//  - .rodata: RO + NX
//  - .data/.bss/.pt/.stack: RW + NX
//  - UART MMIO: Device + NX, minimal window
//  - Guard holes: null page unmapped; first stack page unmapped
//

#include "mmu.h"

#include <stddef.h>
#include <stdint.h>

#include "memattr.h"
#include "linker_symbols.h"
#include "sysreg.h"

/* Provided by your project (mem.c) */
void *memset(void *dst, int c, size_t n);

/* QEMU virt PL011 base (adjust if you target different hardware) */
#ifndef MMU_UART0_BASE
#define MMU_UART0_BASE 0x09000000ull
#endif

#define PAGE_SIZE   4096ull
#define L1_ENTRIES  512
#define L2_ENTRIES  512
#define L3_ENTRIES  512

static inline size_t l1_index(uint64_t va) { return (size_t)((va >> 30) & 0x1FFull); }
static inline size_t l2_index(uint64_t va) { return (size_t)((va >> 21) & 0x1FFull); }
static inline size_t l3_index(uint64_t va) { return (size_t)((va >> 12) & 0x1FFull); }

static inline uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1ull); }
static inline uint64_t align_up(uint64_t v, uint64_t a)   { return (v + a - 1ull) & ~(a - 1ull); }

/* --- TCR_EL1 fields for: 4KB granule, 39-bit VA, TTBR0 only --- */
#define TCR_T0SZ(v)     ((uint64_t)(v) & 0x3Full)
#define TCR_IRGN0(v)    (((uint64_t)(v) & 0x3ull) << 8)
#define TCR_ORGN0(v)    (((uint64_t)(v) & 0x3ull) << 10)
#define TCR_SH0(v)      (((uint64_t)(v) & 0x3ull) << 12)
#define TCR_TG0(v)      (((uint64_t)(v) & 0x3ull) << 14)
#define TCR_IPS(v)      (((uint64_t)(v) & 0x7ull) << 32)

static inline uint64_t make_tcr_el1_for_39bit_4k(void) {
    const uint64_t t0sz  = TCR_T0SZ(25);  /* 39-bit VA */
    const uint64_t irgn0 = TCR_IRGN0(1);  /* WBWA */
    const uint64_t orgn0 = TCR_ORGN0(1);  /* WBWA */
    const uint64_t sh0   = TCR_SH0(3);    /* Inner shareable */
    const uint64_t tg0   = TCR_TG0(0);    /* 4KB */
    const uint64_t ips   = TCR_IPS(0);    /* 32-bit PA */
    return t0sz | irgn0 | orgn0 | sh0 | tg0 | ips;
}

/* Simple bump allocator for 4KB page-table pages inside __pt_base..__pt_end. */
static uint8_t *g_pt_next;
static uint8_t *g_pt_end;

static void *pt_alloc_page(void) {
    uint64_t cur = (uint64_t)(uintptr_t)g_pt_next;
    cur = align_up(cur, PAGE_SIZE);

    if (cur + PAGE_SIZE > (uint64_t)(uintptr_t)g_pt_end) {
        for (;;) { __asm__ volatile("wfe"); }
    }

    g_pt_next = (uint8_t *)(uintptr_t)(cur + PAGE_SIZE);
    void *p = (void *)(uintptr_t)cur;
    memset(p, 0, (size_t)PAGE_SIZE);
    return p;
}

static inline int pte_is_valid(uint64_t pte) { return (pte & PTE_VALID) != 0; }

static uint64_t *get_l2_table(uint64_t *l1, uint64_t va) {
    size_t i1 = l1_index(va);
    uint64_t e = l1[i1];
    if (!pte_is_valid(e)) {
        uint64_t *l2 = (uint64_t *)pt_alloc_page();
        l1[i1] = pte_table_desc((uint64_t)(uintptr_t)l2);
        return l2;
    }
    return (uint64_t *)(uintptr_t)(e & 0x0000FFFFFFFFF000ull);
}

static uint64_t *get_l3_table(uint64_t *l2, uint64_t va) {
    size_t i2 = l2_index(va);
    uint64_t e = l2[i2];
    if (!pte_is_valid(e)) {
        uint64_t *l3 = (uint64_t *)pt_alloc_page();
        l2[i2] = pte_table_desc((uint64_t)(uintptr_t)l3);
        return l3;
    }
    return (uint64_t *)(uintptr_t)(e & 0x0000FFFFFFFFF000ull);
}

static void map_page(uint64_t *l1, uint64_t va, uint64_t pa, uint64_t desc) {
    uint64_t *l2 = get_l2_table(l1, va);
    uint64_t *l3 = get_l3_table(l2, va);
    l3[l3_index(va)] = desc;
}

static void map_range_pages(uint64_t *l1, uint64_t va_start, uint64_t va_end, uint64_t (*pte_fn)(uint64_t)) {
    uint64_t v = align_down(va_start, PAGE_SIZE);
    uint64_t end = align_up(va_end, PAGE_SIZE);
    for (; v < end; v += PAGE_SIZE) {
        map_page(l1, v, v /* identity */, pte_fn(v));
    }
}

void mmu_early_enable(void) {
    g_pt_next = __pt_base;
    g_pt_end  = __pt_end;

    uint64_t *l1 = (uint64_t *)pt_alloc_page();

    /* Device MMIO: UART page only */
    map_page(l1, MMU_UART0_BASE, MMU_UART0_BASE, pte_device_rw_nx(MMU_UART0_BASE));

    /* Kernel code (.vectors + .text): RX */
    map_range_pages(l1,
        (uint64_t)(uintptr_t)__text_start,
        (uint64_t)(uintptr_t)__text_end,
        pte_ktext_rx);

    /* Kernel rodata: RO + NX */
    map_range_pages(l1,
        (uint64_t)(uintptr_t)__rodata_start,
        (uint64_t)(uintptr_t)__rodata_end,
        pte_krodata_ro_nx);

    /* Kernel data+bss: RW + NX */
    map_range_pages(l1,
        (uint64_t)(uintptr_t)__data_start,
        (uint64_t)(uintptr_t)__bss_end,
        pte_kdata_rw_nx);

    /* Reserved page-table region: RW + NX */
    map_range_pages(l1,
        (uint64_t)(uintptr_t)__pt_base,
        (uint64_t)(uintptr_t)__pt_end,
        pte_kdata_rw_nx);

    /* Stack: leave first page unmapped as guard */
    uint64_t stack_lo = (uint64_t)(uintptr_t)__stack_bottom;
    uint64_t stack_hi = (uint64_t)(uintptr_t)__stack_top;
    map_range_pages(l1, stack_lo + PAGE_SIZE, stack_hi, pte_kdata_rw_nx);

    dsb_ishst();

    write_mair_el1(MAIR_VALUE);
    write_tcr_el1(make_tcr_el1_for_39bit_4k());
    isb();

    write_ttbr0_el1((uint64_t)(uintptr_t)l1);
    isb();

    invalidate_tlb_all_el1();

    /* Enable MMU + WXN. Keep caches off. */
    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (1ull << 0);   /* M */
    sctlr |= (1ull << 19);  /* WXN */
    write_sctlr_el1(sctlr);
    isb();

    invalidate_tlb_all_el1();
}

void mmu_enable_caches(void) {
    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (1ull << 2);    /* C */
    sctlr |= (1ull << 12);   /* I */
    sctlr |= (1ull << 19);   /* keep WXN */
    write_sctlr_el1(sctlr);
    isb();
}

// --- Phase 3.0: address-space contract (stubs acceptable) ---

void mmu_kernel_init_global(void) {
    // Phase 3.0: behavior unchanged; still build the single address space.
    mmu_early_enable();
}
