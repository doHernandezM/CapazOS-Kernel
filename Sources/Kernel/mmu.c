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
#include <stdint.h>
#include <stddef.h>

/* Symbols exported by the linker marking the end of the loaded
 * kernel image.  We use this to place the page‑table allocator just
 * after the kernel in physical memory.  See kernel.ld.
 */
extern uint8_t __kernel_image_end[];

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

/* Table entry type bits.  A table descriptor has bits[1:0] = 0b11
 * (value 3), while a block/page descriptor has bits[1:0] = 0b01
 * (value 1).  See the Arm ARM for details.  We use the same
 * bit patterns as in the boot assembly code.
 */
#define DESC_TABLE 0x3ULL
#define DESC_BLOCK 0x1ULL

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
    /* Convert a higher‑half direct‑mapped virtual address back to
     * its physical counterpart.  The mapping is defined by
     * va = HH_PHYS_4000_BASE + (pa - RAM_BASE) for addresses in
     * 0xFFFF800040000000..0xFFFF80007FFFFFFF.  Subtract the base
     * and add the physical offset.  This helper is used to
     * obtain physical pointers for page tables.
     */
    return (va - HH_PHYS_4000_BASE) + RAM_BASE;
}

/* Simple page allocator.  We carve page‑sized chunks of memory out of
 * the area immediately following the kernel image.  The allocator
 * returns a direct‑mapped virtual address, and the mmu_init()
 * routine converts that to a physical address when writing
 * descriptors.  This allocator never frees memory; it is adequate
 * for early page‑table setup.
 */
static uint64_t alloc_phys;  /* current physical allocation pointer */
static uint64_t alloc_virt;  /* corresponding virtual alias */

static void page_alloc_init(void)
{
    /* Compute the end of the kernel image in virtual and physical
     * addresses.  __kernel_image_end resides in the higher‑half
     * mapping.  Subtract the base and add RAM_BASE to obtain the
     * corresponding physical address.  Align both pointers up to
     * a 4KiB boundary.
     */
    uint64_t end_va  = (uint64_t)__kernel_image_end;
    uint64_t end_pa  = virt_to_phys(end_va);
    alloc_phys = (end_pa + 0xFFFULL) & ~0xFFFULL;
    alloc_virt = HH_PHYS_4000_BASE + (alloc_phys - RAM_BASE);
}

static void *page_alloc(void)
{
    /* Allocate one 4KiB page from the bump pointer.  Zero the
     * contents so page tables start out clean.  Returns a
     * higher‑half virtual address.
     */
    uint64_t va = alloc_virt;
    uint64_t pa = alloc_phys;
    alloc_phys += 0x1000ULL;
    alloc_virt += 0x1000ULL;
    /* Zero memory via the direct mapping. */
    volatile uint64_t *p = (volatile uint64_t *)va;
    for (size_t i = 0; i < 512; ++i) {
        p[i] = 0;
    }
    (void)pa; /* silence unused warning if not referenced elsewhere */
    return (void *)va;
}

void mmu_init(void *boot_info)
{
    (void)boot_info;

    /* Initialise the simple page allocator.  This must be done
     * before any allocations.  It uses __kernel_image_end to
     * determine the end of the loaded kernel image and begins
     * carving pages immediately afterwards.
     */
    page_alloc_init();

    /* Allocate a new top‑level L0 table and a first‑level L1 table.
     * These tables live in the higher‑half direct map and will be
     * used exclusively via TTBR1_EL1.  We leave TTBR0_EL1 empty.
     */
    uint64_t *l0 = (uint64_t *)page_alloc();
    uint64_t *l1 = (uint64_t *)page_alloc();

    /* Compute the physical address of the L1 table.  TTBR1_EL1
     * expects a physical pointer.  The helper virt_to_phys() is
     * valid for addresses within the direct map.
     */
    uint64_t l1_pa = virt_to_phys((uint64_t)l1);

    /* Point the L0 entry at index 256 (covering VA range
     * 0xFFFF8000_0000_0000..) to our L1 table.  This descriptor
     * uses bits[1:0]=0b11 to indicate a table entry.
     */
    l0[256] = l1_pa | DESC_TABLE;

    /* Populate L1[0] for the device/MMIO region (PA 0x0000_0000
     * through 0x3FFF_FFFF) with a block descriptor marked as
     * device memory (AttrIndx=1), non‑shareable and accessed.
     */
    uint64_t dev_desc = 0ULL;
    dev_desc |= ATTRINDX_DEVICE;
    dev_desc |= AF;
    dev_desc |= DESC_BLOCK;
    l1[0] = dev_desc;

    /* Populate L1[1] for the RAM region (PA 0x4000_0000 through
     * 0x7FFF_FFFF).  Set AttrIndx=0 for normal memory, mark
     * inner‑shareable and accessed, and set the block bit.
     */
    uint64_t ram_desc = 0x40000000ULL;
    ram_desc |= SH_INNER;
    ram_desc |= AF;
    ram_desc |= DESC_BLOCK;
    l1[1] = ram_desc;

    /* Compute the physical address of the new L0 table. */
    uint64_t l0_pa = virt_to_phys((uint64_t)l0);

    /* Before disabling TTBR0 we must move the stack pointer to
     * its corresponding higher‑half alias.  The current SP still
     * resides in the low virtual address range (0x4000_0000..).
     * Because the new translation tables will disable TTBR0 and
     * leave the low region unmapped, any future stack access via
     * the low VA would fault.  We compute the alias using the
     * identity between physical and virtual addresses in the
     * direct map: va_high = HH_PHYS_4000_BASE + (pa - RAM_BASE).
     * Since the physical address of the stack is sp_low itself
     * (because it is in the RAM region), the conversion simplifies
     * to sp_high = sp_low - RAM_BASE + HH_PHYS_4000_BASE.
     */
    uint64_t sp_low;
    __asm__ volatile ("mov %0, sp" : "=r"(sp_low));
    uint64_t new_sp = (sp_low - RAM_BASE) + HH_PHYS_4000_BASE;

    /* Prepare MAIR and TCR values.  We reuse the boot MAIR and set
     * EPD0 (bit 7) in the TCR to disable TTBR0.  Note that we leave
     * TTBR0 pointing at zero; with EPD0 set the hardware will not
     * walk it.
     */
    uint64_t mair = MAIR_DEFAULT;
    uint64_t tcr  = TCR_BOOT | (1ULL << 7);

    /* Obtain the virtual address of the kernel’s exception vector
     * table.  This symbol lives in the higher‑half text segment.
     */
    extern char kernel_vectors[];
    uint64_t vbar = (uint64_t)kernel_vectors;

    asm volatile (
        /* Move the stack pointer to its high‑half alias. */
        "mov sp, %[newsp]\n"
        /* Ensure prior writes to translation tables are visible. */
        "dsb ish\n"
        /* Disable TTBR0 by writing zero. */
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
        /* Enable MMU (M), data cache (C) and instruction cache (I). */
        "mrs x0, sctlr_el1\n"
        "orr x0, x0, #1\n"
        "orr x0, x0, #(1 << 2)\n"
        "orr x0, x0, #(1 << 12)\n"
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

    /* Translation tables are installed in TTBR1_EL1, TTBR0_EL1 is
     * disabled via TCR.EPD0, the stack now resides in the high
     * half, and the kernel vectors are active.  mmu_init() can
     * return safely.
     */
}