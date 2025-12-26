//
//  mmu_ttbr1.c
//  Capaz
//
//  Phase 3.1: split kernel vs task mappings.
//    - TTBR1: kernel higher-half mappings (global)
//    - TTBR0: boot identity mappings during transition; later locked down
//
//  CRITICAL BOOT RULES:
//    * Everything executed before switching to high-half must live in .text.boot.
//    * Boot code must not touch high-half .bss/.data.
//    * Large constants used by boot code must live in .rodata.boot to avoid
//      literal pools in high-half .rodata.
//

#include "mmu.h"

#include <stddef.h>
#include <stdint.h>

#include "memattr.h"
#include "linker_symbols.h"
#include "sysreg.h"
#include "vm_layout.h"

#ifndef MMU_UART0_BASE
#define MMU_UART0_BASE 0x09000000ull
#endif

#define PAGE_SIZE   4096ull

/* ---------------- Section placement helpers ---------------- */

#define BOOT_TEXT   __attribute__((section(".text.boot")))
#define BOOT_RODATA __attribute__((section(".rodata.boot")))
#define BOOT_BSS    __attribute__((section(".bss.boot")))

/* ---------------- Boot constants (must be in .rodata.boot) ---------------- */

BOOT_RODATA static const uint64_t BOOT_KVA_OFFSET  = (uint64_t)KERNEL_VA_OFFSET;
BOOT_RODATA static const uint64_t BOOT_UART0_KVA   = (uint64_t)KERNEL_MMIO_UART0_BASE;
BOOT_RODATA static const uint64_t BOOT_UART0_PA    = (uint64_t)MMU_UART0_BASE;

/* ---------------- Boot temporaries (must be in .bss.boot) ---------------- */
/*
 * Boot code may not touch high-half globals. Record the final PT allocator cursor
 * in low boot .bss so we can adopt it later from crt0 (after switching to TTBR1).
 */
BOOT_BSS static uint64_t g_boot_pt_next_phys;
BOOT_BSS static uint64_t g_boot_pt_end_phys;

/* ---------------- Runtime allocator state (high-half .bss) ---------------- */
/*
 * These are used after we are in the higher-half and should NOT be accessed
 * from .text.boot code.
 */
static uint8_t *g_pt_next_va;
static uint8_t *g_pt_end_va;

/* ---------------- Common small helpers (inlined) ---------------- */

static inline uint64_t align_down(uint64_t v, uint64_t a) { return v & ~(a - 1ull); }
static inline uint64_t align_up(uint64_t v, uint64_t a)   { return (v + a - 1ull) & ~(a - 1ull); }

static inline size_t l1_index(uint64_t va) { return (size_t)((va >> 30) & 0x1FFull); }
static inline size_t l2_index(uint64_t va) { return (size_t)((va >> 21) & 0x1FFull); }
static inline size_t l3_index(uint64_t va) { return (size_t)((va >> 12) & 0x1FFull); }

/* ---------------- Boot-only helpers (ALL in .text.boot) ---------------- */

BOOT_TEXT static void boot_memset(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
}

typedef struct {
    uint8_t *next;
    uint8_t *end;
} pt_alloc_t;

BOOT_TEXT static void *pt_alloc_page_boot(pt_alloc_t *a) {
    uint64_t cur = (uint64_t)(uintptr_t)a->next;
    cur = align_up(cur, PAGE_SIZE);

    if (cur + PAGE_SIZE > (uint64_t)(uintptr_t)a->end) {
        for (;;) { __asm__ volatile("wfe"); }
    }

    a->next = (uint8_t *)(uintptr_t)(cur + PAGE_SIZE);
    void *p = (void *)(uintptr_t)cur;
    boot_memset(p, 0, (size_t)PAGE_SIZE);
    return p;
}

BOOT_TEXT static inline int pte_is_valid(uint64_t pte) { return (pte & PTE_VALID) != 0; }

typedef uint64_t (*addr_to_pa_fn)(uintptr_t addr);

BOOT_TEXT static uint64_t id_to_pa(uintptr_t a) { return (uint64_t)a; }

BOOT_TEXT static uint64_t boot_pa_to_kva(uint64_t pa) {
    return pa + BOOT_KVA_OFFSET;
}

BOOT_TEXT static uint64_t *get_l2_table_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa, uint64_t *l1, uint64_t va) {
    size_t i1 = l1_index(va);
    uint64_t e = l1[i1];
    if (!pte_is_valid(e)) {
        uint64_t *l2 = (uint64_t *)pt_alloc_page_boot(alloc);
        l1[i1] = pte_table_desc(to_pa((uintptr_t)l2));
        return l2;
    }
    return (uint64_t *)(uintptr_t)(e & 0x0000FFFFFFFFF000ull);
}

BOOT_TEXT static uint64_t *get_l3_table_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa, uint64_t *l2, uint64_t va) {
    size_t i2 = l2_index(va);
    uint64_t e = l2[i2];
    if (!pte_is_valid(e)) {
        uint64_t *l3 = (uint64_t *)pt_alloc_page_boot(alloc);
        l2[i2] = pte_table_desc(to_pa((uintptr_t)l3));
        return l3;
    }
    return (uint64_t *)(uintptr_t)(e & 0x0000FFFFFFFFF000ull);
}

typedef uint64_t (*pte_fn)(uint64_t pa);

BOOT_TEXT static void map_page_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa,
                                   uint64_t *l1, uint64_t va, uint64_t pa,
                                   uint64_t pte_desc) {
    (void)pa; /* PA already embedded in pte_desc (mk(p)). */
    uint64_t *l2 = get_l2_table_boot(alloc, to_pa, l1, va);
    uint64_t *l3 = get_l3_table_boot(alloc, to_pa, l2, va);
    l3[l3_index(va)] = pte_desc;
}

BOOT_TEXT static void map_range_pages_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa, uint64_t *l1,
                                          uint64_t va_start, uint64_t va_end,
                                          uint64_t pa_start, pte_fn mk) {
    uint64_t v = align_down(va_start, PAGE_SIZE);
    uint64_t vend = align_up(va_end, PAGE_SIZE);
    uint64_t p = align_down(pa_start, PAGE_SIZE);

    for (; v < vend; v += PAGE_SIZE, p += PAGE_SIZE) {
        map_page_boot(alloc, to_pa, l1, v, p, mk(p));
    }
}

/* --- TCR_EL1 fields for: 4KB granule, 39-bit VA, TTBR0+TTBR1 split --- */
#define TCR_T0SZ(v)     ((uint64_t)(v) & 0x3Full)
#define TCR_T1SZ(v)     (((uint64_t)(v) & 0x3Full) << 16)

#define TCR_IRGN0(v)    (((uint64_t)(v) & 0x3ull) << 8)
#define TCR_ORGN0(v)    (((uint64_t)(v) & 0x3ull) << 10)
#define TCR_SH0(v)      (((uint64_t)(v) & 0x3ull) << 12)
#define TCR_TG0(v)      (((uint64_t)(v) & 0x3ull) << 14)

#define TCR_IRGN1(v)    (((uint64_t)(v) & 0x3ull) << 24)
#define TCR_ORGN1(v)    (((uint64_t)(v) & 0x3ull) << 26)
#define TCR_SH1(v)      (((uint64_t)(v) & 0x3ull) << 28)
#define TCR_TG1(v)      (((uint64_t)(v) & 0x3ull) << 30)

#define TCR_IPS(v)      (((uint64_t)(v) & 0x7ull) << 32)

BOOT_TEXT static inline uint64_t make_tcr_el1_split_39bit_4k(void) {
    const uint64_t t0sz  = TCR_T0SZ(25);
    const uint64_t t1sz  = TCR_T1SZ(25);

    const uint64_t irgn0 = TCR_IRGN0(1);
    const uint64_t orgn0 = TCR_ORGN0(1);
    const uint64_t sh0   = TCR_SH0(3);
    const uint64_t tg0   = TCR_TG0(0);

    const uint64_t irgn1 = TCR_IRGN1(1);
    const uint64_t orgn1 = TCR_ORGN1(1);
    const uint64_t sh1   = TCR_SH1(3);
    const uint64_t tg1   = TCR_TG1(2);

    const uint64_t ips   = TCR_IPS(0);

    return t0sz | t1sz | irgn0 | orgn0 | sh0 | tg0 | irgn1 | orgn1 | sh1 | tg1 | ips;
}

BOOT_TEXT static inline uint64_t make_ttbr(uint16_t asid, uint64_t root_pa) {
    return ((uint64_t)asid << 48) | (root_pa & 0x0000FFFFFFFFF000ull);
}

/* ---------------- Boot-time bootstrap (in .text.boot) ---------------- */

BOOT_TEXT void mmu_bootstrap(void) {
    /* Allocate page tables from the physical PT region (MMU is off). */
    pt_alloc_t alloc = {
        .next = __pt_base_phys,
        .end  = __pt_end_phys,
    };

    uint64_t *ttbr1_l1 = (uint64_t *)pt_alloc_page_boot(&alloc);
    uint64_t *ttbr0_l1 = (uint64_t *)pt_alloc_page_boot(&alloc);

    /* ----- TTBR1 (kernel higher-half mappings) ----- */
    const uint64_t text_pa0  = (uint64_t)(uintptr_t)__text_start_phys;
    const uint64_t text_pa1  = (uint64_t)(uintptr_t)__text_end_phys;
    const uint64_t ro_pa0    = (uint64_t)(uintptr_t)__rodata_start_phys;
    const uint64_t ro_pa1    = (uint64_t)(uintptr_t)__rodata_end_phys;
    const uint64_t data_pa0  = (uint64_t)(uintptr_t)__data_start_phys;
    const uint64_t bss_pa1   = (uint64_t)(uintptr_t)__bss_end_phys;

    const uint64_t pt_pa0    = (uint64_t)(uintptr_t)__pt_base_phys;
    const uint64_t pt_pa1    = (uint64_t)(uintptr_t)__pt_end_phys;
    const uint64_t stack_pa0 = (uint64_t)(uintptr_t)__stack_bottom_phys;
    const uint64_t stack_pa1 = (uint64_t)(uintptr_t)__stack_top_phys;

    /* Compute higher-half VAs from PAs without touching high-half symbols. */
    const uint64_t text_va0  = boot_pa_to_kva(text_pa0);
    const uint64_t text_va1  = boot_pa_to_kva(text_pa1);
    const uint64_t ro_va0    = boot_pa_to_kva(ro_pa0);
    const uint64_t ro_va1    = boot_pa_to_kva(ro_pa1);
    const uint64_t data_va0  = boot_pa_to_kva(data_pa0);
    const uint64_t bss_va1   = boot_pa_to_kva(bss_pa1);

    const uint64_t pt_va0    = boot_pa_to_kva(pt_pa0);
    const uint64_t pt_va1    = boot_pa_to_kva(pt_pa1);
    const uint64_t stack_va0 = boot_pa_to_kva(stack_pa0);
    const uint64_t stack_va1 = boot_pa_to_kva(stack_pa1);

    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1, text_va0, text_va1, text_pa0, pte_ktext_rx);
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1, ro_va0,   ro_va1,   ro_pa0,   pte_krodata_ro_nx);
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1, data_va0, bss_va1,  data_pa0, pte_kdata_rw_nx);

    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1, pt_va0,   pt_va1,   pt_pa0,   pte_kdata_rw_nx);

    /* Stack: RW+NX with a guard page at bottom. */
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1,
                         stack_va0 + PAGE_SIZE, stack_va1,
                         stack_pa0 + PAGE_SIZE, pte_kdata_rw_nx);

    /* Kernel MMIO: map UART into TTBR1 (kernel VA). */
    map_page_boot(&alloc, id_to_pa, ttbr1_l1,
                  BOOT_UART0_KVA, BOOT_UART0_PA,
                  pte_device_rw_nx(BOOT_UART0_PA));

    /* ----- TTBR0 (boot identity mappings) ----- */
    const uint64_t boot_text0  = (uint64_t)(uintptr_t)__boot_text_start;
    const uint64_t boot_text1  = (uint64_t)(uintptr_t)__boot_text_end;
    const uint64_t boot_ro0    = (uint64_t)(uintptr_t)__boot_rodata_start;
    const uint64_t boot_ro1    = (uint64_t)(uintptr_t)__boot_rodata_end;
    const uint64_t boot_data0  = (uint64_t)(uintptr_t)__boot_data_start;
    const uint64_t boot_data1  = (uint64_t)(uintptr_t)__boot_data_end;
    const uint64_t boot_stack0 = (uint64_t)(uintptr_t)__boot_stack_bottom;
    const uint64_t boot_stack1 = (uint64_t)(uintptr_t)__boot_stack_top;

    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_text0, boot_text1,
                         boot_text0, pte_ktext_rx);

    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_ro0, boot_ro1,
                         boot_ro0, pte_krodata_ro_nx);

    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_data0, boot_data1,
                         boot_data0, pte_kdata_rw_nx);

    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_stack0, boot_stack1,
                         boot_stack0, pte_kdata_rw_nx);

    /* Optional UART identity mapping while still in TTBR0. */
    map_page_boot(&alloc, id_to_pa, ttbr0_l1,
                  BOOT_UART0_PA, BOOT_UART0_PA,
                  pte_device_rw_nx(BOOT_UART0_PA));

    /*
     * Record final allocator cursor in boot .bss BEFORE enabling the MMU.
     * (Do not write high-half globals here.)
     */
    g_boot_pt_next_phys = (uint64_t)(uintptr_t)alloc.next;
    g_boot_pt_end_phys  = (uint64_t)(uintptr_t)alloc.end;

    dsb_ishst();

    write_mair_el1(MAIR_VALUE);
    write_tcr_el1(make_tcr_el1_split_39bit_4k());
    isb();

    write_ttbr1_el1(make_ttbr(0, (uint64_t)(uintptr_t)ttbr1_l1));
    write_ttbr0_el1(make_ttbr(1, (uint64_t)(uintptr_t)ttbr0_l1));
    isb();

    invalidate_tlb_all_el1();

    /* Enable MMU + WXN + PAN (caches still OFF). */
    uint64_t sctlr = read_sctlr_el1();
    sctlr |= (1ull << 0);    /* M */
    sctlr |= (1ull << 19);   /* WXN */
    sctlr |= (1ull << 22);   /* PAN */
    write_sctlr_el1(sctlr);
    isb();

    invalidate_tlb_all_el1();
}

/* ---------------- Post-bootstrap utilities (normal .text) ---------------- */

/*
 * Must be called after switching to TTBR1 (higher-half) and while TTBR0 still
 * maps the boot region, before you “lock down” TTBR0.
 */
void mmu_adopt_boot_pt_allocator(void) {
    g_pt_next_va = (uint8_t *)(uintptr_t)pa_to_kva(g_boot_pt_next_phys);
    g_pt_end_va  = (uint8_t *)(uintptr_t)pa_to_kva(g_boot_pt_end_phys);
}

static void *pt_alloc_page_kernel(pt_alloc_t *a) {
    uint64_t cur = (uint64_t)(uintptr_t)a->next;
    cur = align_up(cur, PAGE_SIZE);

    if (cur + PAGE_SIZE > (uint64_t)(uintptr_t)a->end) {
        for (;;) { __asm__ volatile("wfe"); }
    }

    a->next = (uint8_t *)(uintptr_t)(cur + PAGE_SIZE);
    void *p = (void *)(uintptr_t)cur;

    /* At this point crt0 has run; normal memset is available. */
    extern void *memset(void *dst, int c, size_t n);
    memset(p, 0, (size_t)PAGE_SIZE);
    return p;
}

uint64_t mmu_ttbr0_create_minimal(void) {
    /* Allocate new TTBR0 root in kernel VA; return PA. */
    pt_alloc_t alloc = { .next = g_pt_next_va, .end = g_pt_end_va };

    uint64_t *ttbr0_l1 = (uint64_t *)pt_alloc_page_kernel(&alloc);

    /* default-deny: leave TTBR0 empty. */
    g_pt_next_va = alloc.next;

    return kva_to_pa((uint64_t)(uintptr_t)ttbr0_l1);
}

void mmu_ttbr0_install(uint64_t root_pa, uint16_t asid) {
    write_ttbr0_el1(make_ttbr(asid, root_pa));
    tlbi_aside1is(asid);
}
