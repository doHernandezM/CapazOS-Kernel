/*
 * mmu.c
 *
 * This file provides a minimal MMU setup routine for the Capaz
 * kernel.  Its purpose is to construct a fresh set of translation
 * tables and install them in TTBR1_EL1 while disabling TTBR0_EL1.
 * The initial boot stage maps both low and high addresses via the
 * same L0 table.  That is convenient for bring‑up but does not
 * enforce a default‑deny policy: user space sees all of kernel
 * memory.  Here we allocate a new L0 and L1 table, map only the
 * device region (physical 0x0000_0000–0x3FFF_FFFF) and the kernel
 * RAM region (physical 0x4000_0000–0x7FFF_FFFF) into the higher
 * half virtual address space, and install the new tables with
 * TCR.EPD0=1 so that TTBR0 is not consulted.  This is a stepping
 * stone towards more granular mappings and W^X enforcement.
 */


#include "mmu.h"
#include "dtb.h"
#include <stdint.h>
#include <stddef.h>

/* Symbols exported by the linker marking the end of the loaded
 * image and the end of the runtime footprint (through .bss). We
 * use the runtime end for any "allocate-after-kernel" decisions
 * so that .bss pages are treated as kernel-owned.
 */
extern uint8_t __kernel_runtime_end[];

/*
 * The kernel’s exception vector table lives in kernel_vectors.S.
 * Export its precise range so we can force that mapping executable
 * even when using coarse-grained (2MiB) RAM mappings.
 */
extern char kernel_vectors[];
extern char __kernel_vectors_start[];
extern char __kernel_vectors_end[];

/* Physical base of the kernel image (passed in via --defsym in the
 * linker invocation).  This symbol is not an actual object in the
 * image but resolves to the constant KERNEL_PHYS_BASE.  We declare
 * it as an array of bytes to take its address in C.
 */
extern uint8_t KERNEL_PHYS_BASE[];

/* The high‑half direct map of physical memory starting at
 * 0xFFFF800040000000.  The boot stage aliases physical addresses
 * 0x4000_0000..0x7FFF_FFFF into this region such that
 * va = HH_PHYS_4000_BASE + (pa - 0x4000_0000).  We use this when
 * translating virtual addresses of objects in .bss to their
 * corresponding physical addresses.
 */
#define HH_PHYS_4000_BASE 0xFFFF800040000000ULL

/* Offset of the physical RAM base used by the boot mapping. */
#define RAM_BASE 0x40000000ULL

#define RAM_BLOCK_SIZE (2ULL * 1024 * 1024)
#define RAM_DIRECTMAP_SIZE (512ULL * RAM_BLOCK_SIZE)

static inline uint64_t align_down_2m(uint64_t x) { return x & ~(RAM_BLOCK_SIZE - 1); }
static inline uint64_t align_up_2m(uint64_t x) { return (x + (RAM_BLOCK_SIZE - 1)) & ~(RAM_BLOCK_SIZE - 1); }
/* Table entry type bits.  A table descriptor has bits[1:0] = 0b11
 * (value 3), while a block/page descriptor has bits[1:0] = 0b01
 * (value 1).  See the Arm ARM for details.  We use the same
 * bit patterns as in the boot assembly code.
 */
#define DESC_TABLE 0x3ULL
/* A block descriptor (levels 1 and 2) uses bits[1:0] = 0b01. */
#define DESC_BLOCK 0x1ULL
/* A page descriptor (level 3) also uses bits[1:0] = 0b11.  Do not
 * confuse this with a table descriptor: the rest of the descriptor
 * differs.  We define it explicitly for clarity. */
#define DESC_PAGE 0x3ULL

/* Memory attribute fields.  AttrIndx=0 selects the normal memory
 * attribute (write‑back, write‑allocate), AttrIndx=1 selects
 * device‑nGnRE.  SH values: 0 = non‑shareable, 2 = outer shareable,
 * 3 = inner shareable.  AF=1 marks the entry as accessed.  These
 * match the boot stage values.
 */
#define ATTRINDX_NORMAL  (0ULL << 2)
#define ATTRINDX_DEVICE  (1ULL << 2)
#define SH_INNER         (3ULL << 8)
#define SH_NON           (0ULL << 8)
#define AF               (1ULL << 10)

/* MAIR_EL1 value: Attr0 = 0xFF (normal memory, write‑back
 * write‑allocate), Attr1 = 0x04 (device‑nGnRE).  The boot code
 * programs MAIR_EL1 with 0x04FF, and we reuse the same value here.
 */
#define MAIR_DEFAULT 0x04FFULL

/* TCR_EL1 base value used during boot.  Bits are documented in the
 * Arm ARM.  We take the boot value of 0xB5103510 and set bit 7
 * (EPD0) to 1 to disable walks through TTBR0.  This tells the
 * hardware to ignore TTBR0_EL1 entirely, implementing the
 * default‑deny semantics.  See the comment in start.S for details.
 */
#define TCR_BOOT 0xB5103510ULL

static inline uint64_t virt_to_phys(uint64_t va)
{
    /*
     * Convert a higher-half direct-mapped VA back to PA.
     *
     * During early boot we may still have a low identity-mapped stack
     * (0x4000_0000+...). Accept both aliases.
     */
    if (va >= HH_PHYS_4000_BASE) {
        return (va - HH_PHYS_4000_BASE) + RAM_BASE;
    }
    /* Low alias: treat the VA as already being a physical address. */
    return va;
}

/*
 * Static page tables for the kernel mappings.
 *
 * In earlier versions we carved page tables out of the area
 * immediately following the kernel image using a simple bump
 * allocator.  That proved fragile when the computed end of the
 * kernel image was wrong, leading to page tables overlapping the
 * kernel itself.  To avoid that class of bug, we now reserve
 * dedicated 4 KiB-aligned arrays in .bss for the L0, L1, and L2
 * tables used by TTBR1, as well as a pool of L3 tables.  These
 * tables are zero‑initialised by the loader and are never freed.
 *
 * The maximum number of L3 tables required is bounded by the size
 * of the kernel image.  Each L3 table covers 2 MiB of virtual
 * address space; if the kernel occupies at most 64 MiB the worst
 * case is 32 L3 tables.  We allocate 64 to provide headroom.
 */
static uint64_t l0_table[512] __attribute__((aligned(4096)));
static uint64_t l1_table[512] __attribute__((aligned(4096)));
static uint64_t l2_table[512] __attribute__((aligned(4096)));

static uint64_t l3_pool[64][512] __attribute__((aligned(4096)));
static size_t l3_pool_used = 0;

/* Allocate a fresh L3 page table from the static pool.  Panics if
 * exhausted.  Returns a higher‑half virtual address.  The table is
 * zeroed on allocation.
 */
static uint64_t *alloc_l3(void)
{
    if (l3_pool_used >= (sizeof(l3_pool) / sizeof(l3_pool[0]))) {
        /* Out of L3 tables.  In a real kernel we would handle this
         * gracefully (e.g. recycle tables or panic with a useful
         * message).  For now, spin forever. */
        while (1) {
            __asm__ volatile ("wfe");
        }
    }
    uint64_t *tbl = l3_pool[l3_pool_used++];
    /* Zero the table. */
    for (size_t i = 0; i < 512; ++i) {
        tbl[i] = 0;
    }
    return tbl;
}

/* The bump allocator is retained for future use (e.g. allocating
 * dynamic pages beyond the initial tables), but page table memory is
 * now drawn from the static pool.  We initialise the bump
 * allocator after setting up the static tables, in case later
 * components need additional pages.  */
static uint64_t alloc_phys;  /* current physical allocation pointer */
static uint64_t alloc_virt;  /* corresponding virtual alias */

static void page_alloc_init(void)
{
    /* Compute the end of the kernel image in virtual and physical
     * addresses.  __kernel_runtime_end resides in the higher‑half
     * mapping.  Subtract the base and add RAM_BASE to obtain the
     * corresponding physical address.  Align both pointers up to
     * a 4KiB boundary.
     */
    uint64_t end_va  = (uint64_t)__kernel_runtime_end;
    uint64_t end_pa  = virt_to_phys(end_va);
    alloc_phys = (end_pa + 0xFFFULL) & ~0xFFFULL;
    alloc_virt = HH_PHYS_4000_BASE + (alloc_phys - RAM_BASE);
}

static void *page_alloc(void)
{
    uint64_t va = alloc_virt;
    uint64_t pa = alloc_phys;
    alloc_phys += 0x1000ULL;
    alloc_virt += 0x1000ULL;
    /* Zero memory via the direct mapping. */
    volatile uint64_t *p = (volatile uint64_t *)va;
    for (size_t i = 0; i < 512; ++i) {
        p[i] = 0;
    }
    (void)pa;
    return (void *)va;
}


void mmu_init(const boot_info_t *boot_info)
{
    (void)boot_info;

    /* Initialise the simple page allocator.  This must be done
     * before any allocations.  It uses __kernel_runtime_end (end
     * of runtime footprint, through .bss) to avoid overlap with
     * kernel-owned pages.
     */
    page_alloc_init();

    /* Exported section boundaries from the linker script.  These
     * symbols reside in the higher‑half direct map.  We convert
     * their virtual addresses to physical addresses so that page
     * table entries can point at the correct physical frames.
     */
    extern uint8_t __text_start[], __text_end[];
    extern uint8_t __rodata_start[], __rodata_end[];
    extern uint8_t __data_start[], __data_end[];
    extern uint8_t __bss_start[], __bss_end[];
    extern uint8_t __kernel_image_start[], __kernel_runtime_end[];

    uint64_t text_pa_start   = virt_to_phys((uint64_t)__text_start);
    uint64_t text_pa_end     = virt_to_phys((uint64_t)__text_end);
    uint64_t rodata_pa_start = virt_to_phys((uint64_t)__rodata_start);
    uint64_t rodata_pa_end   = virt_to_phys((uint64_t)__rodata_end);
    uint64_t data_pa_start   = virt_to_phys((uint64_t)__data_start);
    uint64_t data_pa_end     = virt_to_phys((uint64_t)__data_end);
    uint64_t bss_pa_start    = virt_to_phys((uint64_t)__bss_start);
    uint64_t bss_pa_end      = virt_to_phys((uint64_t)__bss_end);
    uint64_t kernel_pa_start = virt_to_phys((uint64_t)__kernel_image_start);
    uint64_t kernel_pa_end   = virt_to_phys((uint64_t)__kernel_runtime_end);

    /* Vector table physical range.  Align the start down to a page
     * boundary and map at least one page.  If kernel_vectors
     * expands beyond 4 KiB in the future, adjust vec_pa_end
     * accordingly. */
    uint64_t vec_pa_start = virt_to_phys((uint64_t)kernel_vectors) & ~0xFFFULL;
    /* Use the statically reserved page tables for TTBR1.  The
     * l0_table, l1_table and l2_table arrays live in .bss and are
     * 4KiB‑aligned.  They are zero‑initialised at load time.  The
     * l3_pool is used for finer‑grained mappings below.
     */
    uint64_t *l0 = l0_table;
    uint64_t *l1 = l1_table;
    uint64_t *l2 = l2_table;

    /* Fill the L0 table: entry 256 (covering 0xFFFF8000_0000_0000..)
     * points to our L1 table.  All other entries remain zero.
     */
    uint64_t l1_pa = virt_to_phys((uint64_t)l1);
    l0[256] = l1_pa | DESC_TABLE;

    /* L1[0]: map the device/MMIO region (physical 0x0000_0000..0x3FFF_FFFF)
     * as a 1‑GiB block of Device memory.  We mark it non‑shareable,
     * accessed, privileged execute never and unprivileged execute
     * never.  Device memory must never be executable.
     */
    uint64_t dev_desc = 0ULL;
    dev_desc |= ATTRINDX_DEVICE;
    dev_desc |= SH_NON;
    dev_desc |= AF;
    dev_desc |= (1ULL << 53) | (1ULL << 54); /* PXN|UXN */
    dev_desc |= DESC_BLOCK;
    l1[0] = dev_desc;

    /* L1[1]: map the RAM region via an L2 table.  Normal memory,
     * inner‑shareable, accessed.  Bits[1:0]=0b11 indicate a table.
     */
    uint64_t l2_pa = virt_to_phys((uint64_t)l2);
    l1[1] = l2_pa | DESC_TABLE;

    /* Build the L2 table.  Each entry covers 2 MiB.  We iterate
     * over the 1 GiB RAM space and create either a 2 MiB block
     * descriptor (for pages outside the kernel image) or a pointer
     * to a newly allocated L3 table (for pages overlapping the
     * kernel image).  Pages in the kernel image are then mapped
     * individually with appropriate permissions in the L3 table.
     */
    /* Map RAM blocks using DTB-provided memory ranges (fallback to legacy RAM_BASE/RAM_DIRECTMAP_SIZE). */
    // Clear the L2 table. Keep this local to mmu.c to avoid depending on
    // arch-specific header constants.
    for (size_t i = 0; i < (sizeof(l2_table) / sizeof(l2_table[0])); i++) {
        l2_table[i] = 0;
    }

    enum { MMU_MAX_MEMORY_RANGES = 64 };
    dtb_range_t mem_ranges[MMU_MAX_MEMORY_RANGES];
    uint32_t mem_count = MMU_MAX_MEMORY_RANGES;
    bool have_ranges = dtb_get_memory_ranges(mem_ranges, &mem_count);
    if (!have_ranges || mem_count == 0) {
        mem_ranges[0].base = RAM_BASE;
        mem_ranges[0].size = RAM_DIRECTMAP_SIZE;
        mem_count = 1;
    }

for (size_t r = 0; r < mem_count; r++) {
    uint64_t range_start = mem_ranges[r].base;
    uint64_t range_end   = mem_ranges[r].base + mem_ranges[r].size;

    /* Clamp to the legacy direct-map window we currently support (RAM_BASE .. RAM_BASE+RAM_DIRECTMAP_SIZE). */
    if (range_end <= RAM_BASE || range_start >= (RAM_BASE + RAM_DIRECTMAP_SIZE)) continue;
    if (range_start < RAM_BASE) range_start = RAM_BASE;
    if (range_end > (RAM_BASE + RAM_DIRECTMAP_SIZE)) range_end = RAM_BASE + RAM_DIRECTMAP_SIZE;

    uint64_t pa = align_down_2m(range_start);
    uint64_t pa_end = align_up_2m(range_end);

    for (; pa < pa_end; pa += RAM_BLOCK_SIZE) {
        size_t i = (size_t)((pa - RAM_BASE) / RAM_BLOCK_SIZE);

        uint64_t region_pa_start = pa;
        uint64_t region_pa_end = pa + RAM_BLOCK_SIZE;

        /* Determine overlap with kernel image. */
        bool overlaps_kernel = !(region_pa_end <= kernel_pa_start || region_pa_start >= kernel_pa_end);

        if (overlaps_kernel) {
            /* Allocate an L3 table and map each 4KB page with correct permissions. */
            uint64_t *l3 = alloc_l3();
            
            uint64_t pa4 = region_pa_start;
            for (size_t p = 0; p < 512; p++) {
                uint64_t desc = pa4;

                /* Common: Normal memory, inner-shareable, accessed. */
                desc |= ATTRINDX_NORMAL;
                desc |= SH_INNER;
                desc |= AF;

                /* Default permissions: RW in EL1, XN. */
                desc |= (0ULL << 6); /* AP_RW_EL1 */
                desc |= (1ULL << 53) | (1ULL << 54); /* PXN|UXN */

                /* Determine which segment this belongs to and apply perms. */
                    if (pa4 == vec_pa_start) {
                        /* Exception vectors: RO, executable. */
                        desc &= ~((1ULL << 53) | (1ULL << 54)); /* clear XN */
                        desc &= ~(3ULL << 6);
                        desc |= (2ULL << 6); /* AP_RO_EL1 */
                    } else if (pa4 >= text_pa_start && pa4 < text_pa_end) {
                    /* Text: RO, executable. */
                    desc &= ~((1ULL << 53) | (1ULL << 54)); /* clear XN */
                    desc &= ~(3ULL << 6);
                    desc |= (2ULL << 6); /* AP_RO_EL1 */
                } else if (pa4 >= rodata_pa_start && pa4 < rodata_pa_end) {
                    /* Rodata: RO, XN. */
                    desc &= ~(3ULL << 6);
                    desc |= (2ULL << 6); /* AP_RO_EL1 */
                } else if (pa4 >= data_pa_start && pa4 < data_pa_end) {
                    /* Data: RW, XN (default). */
                } else if (pa4 >= bss_pa_start && pa4 < bss_pa_end) {
                    /* BSS: RW, XN (default). */
                } else {
                    /* Outside known segments but still within kernel image: RW, XN. */
                }

                desc |= DESC_PAGE;
                l3[p] = desc;
                pa4 += 0x1000;
            }

            uint64_t l3_pa = virt_to_phys((uint64_t)l3);
            l2_table[i] = l3_pa | DESC_TABLE;
        } else {
            /* Non-kernel RAM: map as a 2MiB block, RW, XN (via WXN in SCTLR). */
            uint64_t desc = region_pa_start;
            desc |= ATTRINDX_NORMAL;
            desc |= SH_INNER;
            desc |= AF;
            desc |= (0ULL << 6); /* AP_RW_EL1 */
            desc |= (1ULL << 53) | (1ULL << 54); /* PXN|UXN */
            desc |= DESC_BLOCK;
            l2_table[i] = desc;
        }
    }
}



    /* Compute the physical address of the L0 table. */
    uint64_t l0_pa = virt_to_phys((uint64_t)l0);

    /* Move the stack pointer to its high‑half alias before
     * disabling TTBR0.  Compute the physical address of the current
     * SP via virt_to_phys() (works for low and high aliases) and
     * then form the high‑half alias: new_sp = HH_PHYS_4000_BASE +
     * (sp_phys - RAM_BASE).  This ensures that further stack
     * accesses use the higher‑half mapping which remains valid when
     * TTBR0 is disabled. */
    uint64_t sp_val;
    __asm__ volatile ("mov %0, sp" : "=r"(sp_val));
    uint64_t sp_phys = virt_to_phys(sp_val);
    uint64_t new_sp = HH_PHYS_4000_BASE + (sp_phys - RAM_BASE);

    /* Program MAIR and TCR values.  Set EPD0=1 to disable TTBR0
     * walks.  Reuse the boot TCR value from TCR_BOOT. */
    uint64_t mair = MAIR_DEFAULT;
    uint64_t tcr  = TCR_BOOT | (1ULL << 7);

    /* Obtain the virtual address of the kernel’s exception vector
     * table.  This symbol lives in the higher‑half text segment.
     */
    extern char kernel_vectors[];
    uint64_t vbar = (uint64_t)kernel_vectors;

    asm volatile (
        /* Switch stack to the high‑half alias. */
        "mov sp, %[newsp]\n"
        /* Ensure prior writes to translation tables are visible. */
        "dsb ish\n"
        /* Disable TTBR0 by writing zero. */
        "msr vbar_el1, %[vbar]\n"
        "isb\n"
        "msr ttbr0_el1, xzr\n"
        /* Install the new L0 table into TTBR1. */
        "msr ttbr1_el1, %[l0pa]\n"
        /* Program MAIR_EL1 and TCR_EL1 (with EPD0 set). */
        "msr mair_el1, %[mair]\n"
        "msr tcr_el1, %[tcr]\n"
        "isb\n"
        /* Invalidate old translations. */
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "isb\n"
        /* Enable MMU (M), data cache (C), instruction cache (I) and
         * set WXN=1 to prevent execute on writeable pages. */
        "mrs x0, sctlr_el1\n"
        "orr x0, x0, #1\n"
        "orr x0, x0, #(1 << 2)\n"
        "orr x0, x0, #(1 << 12)\n"
        "orr x0, x0, #(1 << 19)\n" /* WXN */
        "msr sctlr_el1, x0\n"
        "isb\n"
        /* Point VBAR_EL1 at the kernel exception vectors. */
        "msr vbar_el1, %[vbar]\n"
        "isb\n"
        :
        : [newsp] "r"(new_sp),
          [l0pa]  "r"(l0_pa),
          [mair]  "r"(mair),
          [tcr]   "r"(tcr),
          [vbar]  "r"(vbar)
        : "x0", "memory"
    );

    /* New translation tables are active via TTBR1_EL1.  TTBR0_EL1
     * has been disabled, the stack resides in the high half, and
     * the kernel exception vectors are installed.  The memory
     * attributes for the kernel image enforce W^X at page
     * granularity and mark device memory non‑executable.  */
}
