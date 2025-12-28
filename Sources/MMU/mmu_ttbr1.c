// mmu_ttbr1.c
// Phase 3: split kernel vs task mappings.
//   - TTBR1: kernel higher-half mappings (global)
//   - TTBR0: per-task mappings (initially minimal; swappable for tests)
//
// Boot constraints:
//   - All code executed before the higher-half branch must live in .text.boot.
//   - Boot code must not touch higher-half .bss globals.
//   - Large constants used by boot code must live in .rodata.boot.

#include "mmu.h"

#include <stddef.h>
#include <stdint.h>

#include "memattr.h"
#include "linker_symbols.h"
#include "sysreg.h"
#include "vm_layout.h"

/*
 * When building the boot image the kernel linker symbols such as
 * __text_start_phys, __bss_end_phys, etc. are not available.  To
 * construct the higher-half mappings without taking addresses of
 * kernel-linked symbols, use constants defined in boot_config.h to
 * describe the kernel's physical load address and size.  This header
 * provides KERNEL_PHYS_BASE and KERNEL_PHYS_END which the boot
 * bootstrap uses to map the entire kernel image as a single range.
 */
#include "boot_config.h"

/*
 * When compiling the runtime kernel image (BOOT_STAGE undefined), the
 * macros describing the kernel's physical layout (KERNEL_PHYS_*) may
 * not be provided on the command line.  Provide fallback definitions
 * here so that references to these macros compile without error.
 */
#ifndef KERNEL_PHYS_BASE
#define KERNEL_PHYS_BASE        0ull
#endif
#ifndef KERNEL_PHYS_END
#define KERNEL_PHYS_END         0ull
#endif
#ifndef KERNEL_PHYS_TEXT_START
#define KERNEL_PHYS_TEXT_START  0ull
#endif
#ifndef KERNEL_PHYS_TEXT_END
#define KERNEL_PHYS_TEXT_END    0ull
#endif
#ifndef KERNEL_PHYS_RODATA_START
#define KERNEL_PHYS_RODATA_START 0ull
#endif
#ifndef KERNEL_PHYS_RODATA_END
#define KERNEL_PHYS_RODATA_END   0ull
#endif
#ifndef KERNEL_PHYS_DATA_START
#define KERNEL_PHYS_DATA_START  0ull
#endif
#ifndef KERNEL_PHYS_BSS_END
#define KERNEL_PHYS_BSS_END     0ull
#endif
#ifndef KERNEL_PHYS_PT_BASE
#define KERNEL_PHYS_PT_BASE     0ull
#endif
#ifndef KERNEL_PHYS_PT_END
#define KERNEL_PHYS_PT_END      0ull
#endif
#ifndef KERNEL_PHYS_STACK_BASE
#define KERNEL_PHYS_STACK_BASE  0ull
#endif
#ifndef KERNEL_PHYS_STACK_END
#define KERNEL_PHYS_STACK_END   0ull
#endif

/*
 * In the runtime kernel (BOOT_STAGE undefined) the page-table allocator
 * needs the start and end of the kernel's page-table arena.  These
 * symbols are normally declared by <linker_symbols.h> when building
 * the kernel image.  However, some build systems (e.g. Xcode) may not
 * pick up the updated header search paths, leading to __pt_base and
 * __pt_end being undeclared.  To make the allocator robust across
 * build environments we redeclare them here when BOOT_STAGE is not
 * defined.  They are defined by the linker script (kernel.ld) and
 * imported as 8-bit symbols.
 */
#ifndef BOOT_STAGE
extern uint8_t __pt_base[];
extern uint8_t __pt_end[];
#endif

#ifndef MMU_UART0_BASE
#define MMU_UART0_BASE 0x09000000ull
#endif

#define PAGE_SIZE   4096ull

/* Reserve some PT pages for bootstrap so runtime can start from a fixed cursor
 * (avoids boot->runtime state handoff across sections). Tune as needed. */
#ifndef MMU_BOOT_PT_RESERVE_PAGES
#define MMU_BOOT_PT_RESERVE_PAGES 12u
#endif

#if defined(__clang__)
#define BOOT_TARGET_GENERAL_REGS __attribute__((target("general-regs-only")))
#else
#define BOOT_TARGET_GENERAL_REGS
#endif

#define BOOT_TEXT   __attribute__((section(".text.boot"))) __attribute__((noinline)) BOOT_TARGET_GENERAL_REGS
#define BOOT_RODATA __attribute__((section(".rodata.boot"))) __attribute__((used))

/* ---------------- Boot-only constants (kept close to .text.boot) ---------------- */
/*
 * The higher-half VA offset must match the linker script's __kern_offset.
 * Using the linker-provided symbol avoids silent divergence between vm_layout.h
 * and the link map.
 */
/*
 * During boot we map the kernel at a higher‑half virtual address that is
 * independent of its physical load address.  Compute the VA offset as
 * the difference between the configured KERNEL_VA_BASE and the actual
 * physical base (KERNEL_PHYS_BASE) provided by the build script.  Using
 * KERNEL_VA_OFFSET (which assumes KERNEL_PA_BASE = 0x4000_0000) would
 * be incorrect when the kernel is loaded at a different physical base.
 */
static BOOT_RODATA const uint64_t kBootKvaOffset =
    ((uint64_t)KERNEL_VA_BASE) - ((uint64_t)KERNEL_PHYS_BASE);
static BOOT_RODATA const uint64_t kBootMairEl1   = (uint64_t)MAIR_VALUE;

/* Precomputed TCR_EL1 for: 4KB granule, 39-bit VA, TTBR0+TTBR1 split, 32-bit PA. */
static BOOT_RODATA const uint64_t kBootTcrEl1 =
    /* T0SZ=25, T1SZ=25 */ ((uint64_t)25) | (((uint64_t)25) << 16) |
    /* IRGN0/ORGN0=WBWA, SH0=inner, TG0=4KB */ (((uint64_t)1) << 8) | (((uint64_t)1) << 10) | (((uint64_t)3) << 12) | (((uint64_t)0) << 14) |
    /* IRGN1/ORGN1=WBWA, SH1=inner, TG1=4KB */ (((uint64_t)1) << 24) | (((uint64_t)1) << 26) | (((uint64_t)3) << 28) | (((uint64_t)2) << 30) |
    /* IPS=0 (32-bit PA) */ (((uint64_t)0) << 32);

static BOOT_RODATA const uint64_t kBootKernelUartVa = (uint64_t)KERNEL_MMIO_UART0_BASE;

/* ---------------- Small helpers ---------------- */
static BOOT_TEXT uint64_t boot_pa_to_kva(uint64_t pa) { return pa + kBootKvaOffset; }

static inline uint64_t align_down_u64(uint64_t v, uint64_t a) { return v & ~(a - 1ull); }
static inline uint64_t align_up_u64(uint64_t v, uint64_t a)   { return (v + a - 1ull) & ~(a - 1ull); }

/*
 * Boot code must not form PC-relative references to higher-half .text.
 * Avoid relying on inlining (-O0 can spill helpers into .text) by using
 * boot-local macros for address arithmetic and indices.
 */
#define BOOT_ALIGN_DOWN_U64(v, a) ((uint64_t)(v) & ~((uint64_t)(a) - 1ull))
#define BOOT_ALIGN_UP_U64(v, a)   (((uint64_t)(v) + (uint64_t)(a) - 1ull) & ~((uint64_t)(a) - 1ull))

#define BOOT_L1_INDEX(va) ((size_t)(((uint64_t)(va) >> 30) & 0x1FFull))
#define BOOT_L2_INDEX(va) ((size_t)(((uint64_t)(va) >> 21) & 0x1FFull))
#define BOOT_L3_INDEX(va) ((size_t)(((uint64_t)(va) >> 12) & 0x1FFull))

#define BOOT_PTE_IS_VALID(pte) (((pte) & PTE_VALID) != 0)

static BOOT_TEXT void boot_memset(void *dst, int c, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c;
}

/* ---------------- Boot-time page table builder (physical pointers) ---------------- */

typedef struct {
    uint8_t *next;
    uint8_t *end;
} pt_alloc_t;

static BOOT_TEXT void *pt_alloc_page_boot(pt_alloc_t *a) {
    uint64_t cur = (uint64_t)(uintptr_t)a->next;
    cur = BOOT_ALIGN_UP_U64(cur, PAGE_SIZE);

    if (cur + PAGE_SIZE > (uint64_t)(uintptr_t)a->end) {
        for (;;) { __asm__ volatile("wfe"); }
    }

    a->next = (uint8_t *)(uintptr_t)(cur + PAGE_SIZE);
    void *p = (void *)(uintptr_t)cur;
    boot_memset(p, 0, (size_t)PAGE_SIZE);
    return p;
}

typedef uint64_t (*addr_to_pa_fn)(uintptr_t addr);
static BOOT_TEXT uint64_t id_to_pa(uintptr_t a) { return (uint64_t)a; }

/* Boot-local descriptor constructors (avoid calling header inlines from boot). */
static BOOT_TEXT uint64_t boot_pte_table_desc(uint64_t next_level_table_pa) {
    return PTE_TYPE_TABLE | PTE_TABLE_ADDR(next_level_table_pa);
}

/* PTE constructors used as function pointers must live in .text.boot. */
static BOOT_TEXT uint64_t boot_pte_ktext_rx(uint64_t pa) {
    return PTE_TYPE_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | PTE_SH_INNER
         | PTE_AP_RO_EL1
         | PTE_ATTRINDX(MAIR_IDX_NORMAL);
}

static BOOT_TEXT uint64_t boot_pte_krodata_ro_nx(uint64_t pa) {
    return PTE_TYPE_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | PTE_SH_INNER
         | PTE_AP_RO_EL1
         | PTE_ATTRINDX(MAIR_IDX_NORMAL)
         | (PTE_PXN | PTE_UXN);
}

static BOOT_TEXT uint64_t boot_pte_kdata_rw_nx(uint64_t pa) {
    return PTE_TYPE_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | PTE_SH_INNER
         | PTE_AP_RW_EL1
         | PTE_ATTRINDX(MAIR_IDX_NORMAL)
         | (PTE_PXN | PTE_UXN);
}

static BOOT_TEXT uint64_t boot_pte_device_rw_nx_desc(uint64_t pa) {
    return PTE_TYPE_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | PTE_SH_OUTER
         | PTE_AP_RW_EL1
         | PTE_ATTRINDX(MAIR_IDX_DEVICE)
         | (PTE_PXN | PTE_UXN);
}

/*
 * Construct a PTE suitable for mapping the entire kernel image during
 * boot.  The kernel is mapped with read/write/execute permissions
 * because its sections (text, data and bss) share a contiguous
 * physical region.  We use the normal memory attribute index and
 * grant RW at EL1.  Execute is allowed by omitting PTE_PXN and
 * PTE_UXN.  Note that this is only used for the boot-time higher-half
 * mapping; the runtime kernel builds its own page tables with finer
 * granularity.
 */
static BOOT_TEXT uint64_t boot_pte_kernel_rw_x(uint64_t pa) {
    return PTE_TYPE_PAGE
         | PTE_PAGE_ADDR(pa)
         | PTE_AF
         | PTE_SH_INNER
         | PTE_AP_RW_EL1
         | PTE_ATTRINDX(MAIR_IDX_NORMAL);
}

static BOOT_TEXT uint64_t *get_l2_table_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa, uint64_t *l1, uint64_t va) {
    size_t i1 = BOOT_L1_INDEX(va);
    uint64_t e = l1[i1];
    if (!BOOT_PTE_IS_VALID(e)) {
        uint64_t *l2 = (uint64_t *)pt_alloc_page_boot(alloc);
        l1[i1] = boot_pte_table_desc(to_pa((uintptr_t)l2));
        return l2;
    }
    return (uint64_t *)(uintptr_t)(e & 0x0000FFFFFFFFF000ull);
}

static BOOT_TEXT uint64_t *get_l3_table_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa, uint64_t *l2, uint64_t va) {
    size_t i2 = BOOT_L2_INDEX(va);
    uint64_t e = l2[i2];
    if (!BOOT_PTE_IS_VALID(e)) {
        uint64_t *l3 = (uint64_t *)pt_alloc_page_boot(alloc);
        l2[i2] = boot_pte_table_desc(to_pa((uintptr_t)l3));
        return l3;
    }
    return (uint64_t *)(uintptr_t)(e & 0x0000FFFFFFFFF000ull);
}

static BOOT_TEXT void map_page_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa, uint64_t *l1,
                                   uint64_t va, uint64_t pa, uint64_t pte_desc) {
    (void)pa; /* PA is embedded in pte_desc by constructors; kept for clarity. */
    uint64_t *l2 = get_l2_table_boot(alloc, to_pa, l1, va);
    uint64_t *l3 = get_l3_table_boot(alloc, to_pa, l2, va);
    l3[BOOT_L3_INDEX(va)] = pte_desc;
}

typedef uint64_t (*pte_fn)(uint64_t pa);

static BOOT_TEXT void map_range_pages_boot(pt_alloc_t *alloc, addr_to_pa_fn to_pa, uint64_t *l1,
                                          uint64_t va_start, uint64_t va_end,
                                          uint64_t pa_start, pte_fn mk) {
    uint64_t v = BOOT_ALIGN_DOWN_U64(va_start, PAGE_SIZE);
    uint64_t vend = BOOT_ALIGN_UP_U64(va_end, PAGE_SIZE);
    uint64_t p = BOOT_ALIGN_DOWN_U64(pa_start, PAGE_SIZE);

    for (; v < vend; v += PAGE_SIZE, p += PAGE_SIZE) {
        map_page_boot(alloc, to_pa, l1, v, p, mk(p));
    }
}

static inline uint64_t make_ttbr(uint16_t asid, uint64_t root_pa) {
    return ((uint64_t)asid << 48) | (root_pa & 0x0000FFFFFFFFF000ull);
}

/* Boot path must not call or take addresses of helpers that might land in higher-half .text. */
static BOOT_TEXT uint64_t make_ttbr_boot(uint16_t asid, uint64_t root_pa) {
    return ((uint64_t)asid << 48) | (root_pa & 0x0000FFFFFFFFF000ull);
}

/* ---------------- Boot-time bootstrap (runs with MMU off) ---------------- */

/*
 * mmu_bootstrap constructs initial page tables for both TTBR0 (identity
 * mapping for boot code) and TTBR1 (higher-half mapping for the kernel) and
 * enables the MMU. It is only used in the early boot stage. When
 * compiling the runtime kernel image the definition is omitted.
 */
#ifdef BOOT_STAGE
__attribute__((section(".text.boot"))) BOOT_TARGET_GENERAL_REGS
void mmu_bootstrap(void) {
    /*
     * Allocate bootstrap page tables from a local arena.  The boot image
     * cannot reference the kernel's page-table arena symbols since they
     * are defined in the kernel linker script.  Reserving a small
     * static buffer in .bss for boot page tables avoids the need for
     * __pt_base_phys/__pt_end_phys in the boot image.  Adjust
     * BOOT_PT_PAGES if you need more PT pages during early boot.
     */

    /* Number of pages to reserve for bootstrap page tables (defaults to 32
     * pages = 128KiB).  Each page is 4KiB. */
#ifndef BOOT_PT_PAGES
#define BOOT_PT_PAGES 32u
#endif
    static uint8_t boot_pt_arena[PAGE_SIZE * BOOT_PT_PAGES] __attribute__((aligned(PAGE_SIZE)));

    pt_alloc_t alloc = {
        .next = boot_pt_arena,
        .end  = boot_pt_arena + sizeof(boot_pt_arena),
    };

    /* Root tables are physical pointers in bootstrap context. */
    uint64_t *ttbr1_l1 = (uint64_t *)pt_alloc_page_boot(&alloc);
    uint64_t *ttbr0_l1 = (uint64_t *)pt_alloc_page_boot(&alloc);

    /* ----- TTBR1 (kernel higher-half) ----- */
    /* Map kernel sections separately to honour WXN.  Text is mapped
     * read+execute (RX), rodata is mapped read-only and non-executable,
     * and data+bss are mapped read/write and non-executable.  Page
     * tables and the kernel stack are also mapped RW+NX.  All
     * addresses are supplied by boot_config.h. */
    const uint64_t text_pa0   = (uint64_t)KERNEL_PHYS_TEXT_START;
    const uint64_t text_pa1   = (uint64_t)KERNEL_PHYS_TEXT_END;
    const uint64_t text_va0   = boot_pa_to_kva(text_pa0);
    const uint64_t text_va1   = boot_pa_to_kva(text_pa1);
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1,
                         text_va0, text_va1,
                         text_pa0, boot_pte_ktext_rx);

    const uint64_t ro_pa0     = (uint64_t)KERNEL_PHYS_RODATA_START;
    const uint64_t ro_pa1     = (uint64_t)KERNEL_PHYS_RODATA_END;
    const uint64_t ro_va0     = boot_pa_to_kva(ro_pa0);
    const uint64_t ro_va1     = boot_pa_to_kva(ro_pa1);
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1,
                         ro_va0, ro_va1,
                         ro_pa0, boot_pte_krodata_ro_nx);

    /* Map the .got region (between rodata_end and data_start) as RW+NX.
     * The kernel's Global Offset Table sits between the rodata and data
     * sections.  Without mapping this range the CPU will fault when
     * attempting to load function pointers through the GOT.  We treat
     * the GOT as data and map it writable and non-executable. */
    const uint64_t got_pa0   = (uint64_t)KERNEL_PHYS_RODATA_END;
    const uint64_t got_pa1   = (uint64_t)KERNEL_PHYS_DATA_START;
    if (got_pa1 > got_pa0) {
        const uint64_t got_va0   = boot_pa_to_kva(got_pa0);
        const uint64_t got_va1   = boot_pa_to_kva(got_pa1);
        map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1,
                             got_va0, got_va1,
                             got_pa0, boot_pte_kdata_rw_nx);
    }

    /* data+bss form one contiguous PA range starting at data_start.  Map
     * both sections with RW+NX permissions.  Note that this range starts
     * at KERNEL_PHYS_DATA_START, which comes after the .got region. */
    const uint64_t data_pa0   = (uint64_t)KERNEL_PHYS_DATA_START;
    const uint64_t bss_pa1    = (uint64_t)KERNEL_PHYS_BSS_END;
    const uint64_t data_va0   = boot_pa_to_kva(data_pa0);
    const uint64_t bss_va1    = boot_pa_to_kva(bss_pa1);
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1,
                         data_va0, bss_va1,
                         data_pa0, boot_pte_kdata_rw_nx);

    /* Map the kernel's page-table region (PT) as RW+NX. */
    const uint64_t pt_pa0     = (uint64_t)KERNEL_PHYS_PT_BASE;
    const uint64_t pt_pa1     = (uint64_t)KERNEL_PHYS_PT_END;
    const uint64_t pt_va0     = boot_pa_to_kva(pt_pa0);
    const uint64_t pt_va1     = boot_pa_to_kva(pt_pa1);
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1,
                         pt_va0, pt_va1,
                         pt_pa0, boot_pte_kdata_rw_nx);

    /* Map the kernel stack as RW+NX, leaving a guard page at the bottom. */
    const uint64_t stack_pa0  = (uint64_t)KERNEL_PHYS_STACK_BASE;
    const uint64_t stack_pa1  = (uint64_t)KERNEL_PHYS_STACK_END;
    const uint64_t stack_va0  = boot_pa_to_kva(stack_pa0);
    const uint64_t stack_va1  = boot_pa_to_kva(stack_pa1);
    map_range_pages_boot(&alloc, id_to_pa, ttbr1_l1,
                         stack_va0 + PAGE_SIZE, stack_va1,
                         stack_pa0 + PAGE_SIZE, boot_pte_kdata_rw_nx);

    /* Map kernel MMIO UART into TTBR1 region. */
    map_page_boot(&alloc, id_to_pa, ttbr1_l1,
                  kBootKernelUartVa, (uint64_t)MMU_UART0_BASE,
                  boot_pte_device_rw_nx_desc((uint64_t)MMU_UART0_BASE));

    /* ----- TTBR0 (boot identity mapping only) ----- */
    const uint64_t boot_text0  = (uint64_t)(uintptr_t)__boot_text_start;
    const uint64_t boot_text1  = (uint64_t)(uintptr_t)__boot_text_end;
    const uint64_t boot_ro0    = (uint64_t)(uintptr_t)__boot_rodata_start;
    const uint64_t boot_ro1    = (uint64_t)(uintptr_t)__boot_rodata_end;
    const uint64_t boot_data0  = (uint64_t)(uintptr_t)__boot_data_start;
    const uint64_t boot_data1  = (uint64_t)(uintptr_t)__boot_data_end;
    const uint64_t boot_stack0 = (uint64_t)(uintptr_t)__boot_stack_bottom;
    const uint64_t boot_stack1 = (uint64_t)(uintptr_t)__boot_stack_top;

    /* Identity-map boot code as RX. */
    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_text0, boot_text1,
                         boot_text0, boot_pte_ktext_rx);

    /* Boot rodata as RO+NX. */
    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_ro0, boot_ro1,
                         boot_ro0, boot_pte_krodata_ro_nx);

    /* Boot data as RW+NX. */
    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_data0, boot_data1,
                         boot_data0, boot_pte_kdata_rw_nx);

    /* Boot stack as RW+NX. */
    map_range_pages_boot(&alloc, id_to_pa, ttbr0_l1,
                         boot_stack0, boot_stack1,
                         boot_stack0, boot_pte_kdata_rw_nx);

    /* Optional: map UART phys in TTBR0 as well (for early prints before switching VA). */
    map_page_boot(&alloc, id_to_pa, ttbr0_l1,
                  (uint64_t)MMU_UART0_BASE, (uint64_t)MMU_UART0_BASE,
                  boot_pte_device_rw_nx_desc((uint64_t)MMU_UART0_BASE));

    dsb_ishst();

    write_mair_el1(kBootMairEl1);
    write_tcr_el1(kBootTcrEl1);
    isb();

    write_ttbr1_el1(make_ttbr_boot(0, (uint64_t)(uintptr_t)ttbr1_l1));
    write_ttbr0_el1(make_ttbr_boot(1, (uint64_t)(uintptr_t)ttbr0_l1));
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

    /* NOTE: No higher-half global state touched here. Runtime allocator is
     * initialized lazily from higher-half addresses after crt0 clears .bss. */
}
#endif /* BOOT_STAGE */

/* ---------------- Runtime allocator (higher-half) ---------------- */

/* ---------------- Runtime allocator and post-bootstrap functions --------- */
/*
 * When compiling the runtime kernel image (BOOT_STAGE undefined), the
 * following symbols implement a simple page-table allocator and
 * utilities to construct and install per-task TTBR0 mappings. The
 * boot image does not include these helpers since it only needs
 * mmu_bootstrap.
 */
#ifndef BOOT_STAGE

static uint8_t *g_pt_next_va;
static uint8_t *g_pt_end_va;

static void pt_allocator_init_if_needed(void) {
    if (g_pt_end_va != NULL) return;

    /* Start after a fixed bootstrap reserve to avoid needing boot-state handoff. */
    g_pt_next_va = __pt_base + (MMU_BOOT_PT_RESERVE_PAGES * PAGE_SIZE);
    g_pt_end_va  = __pt_end;
}

extern void *memset(void *dst, int c, size_t n);

static void *pt_alloc_page_kernel(void) {
    pt_allocator_init_if_needed();

    uint64_t cur = (uint64_t)(uintptr_t)g_pt_next_va;
    cur = align_up_u64(cur, PAGE_SIZE);
    if (cur + PAGE_SIZE > (uint64_t)(uintptr_t)g_pt_end_va) {
        for (;;) { __asm__ volatile("wfe"); }
    }

    g_pt_next_va = (uint8_t *)(uintptr_t)(cur + PAGE_SIZE);

    void *p = (void *)(uintptr_t)cur;
    (void)memset(p, 0, (size_t)PAGE_SIZE);
    return p;
}

/* ---------------- Post-bootstrap utilities ---------------- */

uint64_t mmu_ttbr0_create_minimal(void) {
    pt_allocator_init_if_needed();

    uint64_t *ttbr0_l1 = (uint64_t *)pt_alloc_page_kernel();

    /* Default-deny user space: leave TTBR0 empty. */
    return kva_to_pa((uint64_t)(uintptr_t)ttbr0_l1);
}

void mmu_ttbr0_install(uint64_t root_pa, uint16_t asid) {
    write_ttbr0_el1(make_ttbr(asid, root_pa));
    tlbi_aside1is(asid);
}
#endif /* !BOOT_STAGE */
