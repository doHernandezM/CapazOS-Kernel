// Kernel MMU reconfiguration: rebuild TTBR1 with fine-grained mappings and W^X.

#include "mmu.h"

#include <stddef.h>
#include <stdint.h>

// Linker-provided section boundaries (virtual addresses).
extern uint8_t __kernel_header_start[];
extern uint8_t __kernel_header_end[];
extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __bss_end[];

// -----------------------------------------------------------------------------
// Addressing / layout
// -----------------------------------------------------------------------------

// High-half direct map alias: VA = HH_PHYS_BASE + PA.
// Boot tables already provide this alias; we preserve it when rebuilding TTBR1.
#ifndef HH_PHYS_BASE
#define HH_PHYS_BASE 0xFFFF800000000000ULL
#endif

// RAM is expected to start at 0x4000_0000 (QEMU virt / many SBCs).
#ifndef RAM_BASE
#define RAM_BASE 0x40000000ULL
#endif

// We currently map the first 1 GiB of RAM (0x4000_0000 .. 0x7FFF_FFFF)
// plus the low 1 GiB device window (0x0000_0000 .. 0x3FFF_FFFF).
#define ONE_GIB 0x40000000ULL

// Granule sizes for 4 KiB translation.
#define SZ_4K  0x1000ULL
#define SZ_2M  0x200000ULL

// -----------------------------------------------------------------------------
// Stage-1 descriptor bits (AArch64, 4 KiB granule)
// -----------------------------------------------------------------------------

#define DESC_INVALID 0x0ULL
#define DESC_BLOCK   0x1ULL  // L1/L2
#define DESC_TABLE   0x3ULL  // L0/L1/L2 table, or L3 page

// Attribute index
#define ATTRINDX(n)  (((uint64_t)(n) & 0x7ULL) << 2)

// Access permissions (AP[2:1] at bits [7:6])
#define AP_RW_EL1    (0ULL << 6)      // EL1 RW, EL0 no access
#define AP_RO_EL1    (2ULL << 6)      // EL1 RO, EL0 no access

// Shareability (SH bits [9:8])
#define SH_NON       (0ULL << 8)
#define SH_INNER     (3ULL << 8)

// Access Flag
#define AF           (1ULL << 10)

// Execute-never
#define PXN          (1ULL << 53)
#define UXN          (1ULL << 54)

// -----------------------------------------------------------------------------
// Linker-provided segment boundaries (virtual addresses)
// -----------------------------------------------------------------------------

extern char __kernel_image_start[];
//extern char __kernel_header_start[];
//extern char __kernel_header_end[];
//
//extern char __text_start[];
//extern char __text_end[];
//
//extern char __rodata_start[];
//extern char __rodata_end[];
//
//extern char __data_start[];
//extern char __data_end[];

extern char __bss_start[];
//extern char __bss_end[];

extern void kernel_vectors(void);

// -----------------------------------------------------------------------------
// Minimal page-table allocator (early bump allocator)
// -----------------------------------------------------------------------------

// Enough for: L0 + L1 + L2 + a handful of L3 tables covering the kernel image.
// Increase if you later split more regions.
#define PT_POOL_PAGES 128

static uint8_t g_pt_pool[PT_POOL_PAGES * SZ_4K] __attribute__((aligned(4096)));
static size_t g_pt_pool_off = 0;

static void bzero(void *ptr, size_t n) {
  volatile uint8_t *p = (volatile uint8_t *)ptr;
  for (size_t i = 0; i < n; i++) {
    p[i] = 0;
  }
}

static inline uint64_t kva_to_pa(const void *kva) {
  return (uint64_t)kva - HH_PHYS_BASE;
}

static uint64_t *pt_alloc_table(void) {
  if ((g_pt_pool_off + SZ_4K) > sizeof(g_pt_pool)) {
    // If we run out, crash deterministically rather than scribbling.
    for (;;) {
      __asm__ volatile("wfe");
    }
  }
  uint64_t *tbl = (uint64_t *)(g_pt_pool + g_pt_pool_off);
  g_pt_pool_off += SZ_4K;
  bzero(tbl, SZ_4K);
  return tbl;
}

// -----------------------------------------------------------------------------
// Table helpers
// -----------------------------------------------------------------------------

static inline uint64_t pte_table(uint64_t next_table_pa) {
  return (next_table_pa & 0x0000FFFFFFFFF000ULL) | DESC_TABLE;
}

static inline uint64_t pte_block(uint64_t out_pa, uint64_t attrs) {
  return (out_pa & 0x0000FFFFFFFFF000ULL) | attrs | DESC_BLOCK;
}

static inline uint64_t pte_page(uint64_t out_pa, uint64_t attrs) {
  return (out_pa & 0x0000FFFFFFFFF000ULL) | attrs | DESC_TABLE;
}

static inline uint64_t idx_l0(uint64_t va) { return (va >> 39) & 0x1FFULL; }
static inline uint64_t idx_l1(uint64_t va) { return (va >> 30) & 0x1FFULL; }
static inline uint64_t idx_l2(uint64_t va) { return (va >> 21) & 0x1FFULL; }
static inline uint64_t idx_l3(uint64_t va) { return (va >> 12) & 0x1FFULL; }

// Split a 2 MiB block mapping into an L3 table with identical per-page attributes.
static uint64_t *split_l2_block_to_l3(uint64_t l2_entry, uint64_t base_va, uint64_t default_page_attrs) {
  uint64_t block_pa = l2_entry & 0x0000FFFFFFFFF000ULL;
  uint64_t *l3 = pt_alloc_table();
  uint64_t l3_pa = kva_to_pa(l3);

  (void)base_va;
  for (uint64_t i = 0; i < 512; i++) {
    uint64_t pa = block_pa + i * SZ_4K;
    l3[i] = pte_page(pa, default_page_attrs);
  }
  // Caller will install pte_table(l3_pa) into L2.
  (void)l3_pa;
  return l3;
}

// -----------------------------------------------------------------------------
// Attribute policy (W^X)
// -----------------------------------------------------------------------------

// MAIR indices: 0 = Normal, 1 = Device (nGnRE)
static const uint64_t ATTR_NORMAL_BASE = ATTRINDX(0) | SH_INNER | AF;
static const uint64_t ATTR_DEVICE_BASE = ATTRINDX(1) | SH_NON | AF;

static const uint64_t ATTR_NORMAL_RW_XN = ATTR_NORMAL_BASE | AP_RW_EL1 | PXN | UXN;
static const uint64_t ATTR_NORMAL_RO_XN = ATTR_NORMAL_BASE | AP_RO_EL1 | PXN | UXN;
static const uint64_t ATTR_NORMAL_RO_RX = ATTR_NORMAL_BASE | AP_RO_EL1;  // executable

static const uint64_t ATTR_DEVICE_RW_XN = ATTR_DEVICE_BASE | AP_RW_EL1 | PXN | UXN;

static inline uint64_t va_of_pa(uint64_t pa) {
  return HH_PHYS_BASE + pa;
}

static inline uint64_t pa_of_kva(const void *kva) {
  return (uint64_t)kva - HH_PHYS_BASE;
}

static inline int in_range(uint64_t x, uint64_t a, uint64_t b) {
  return (x >= a) && (x < b);
}

static uint64_t attrs_for_kernel_pa(uint64_t pa,
                                   uint64_t hdr_start_pa,
                                   uint64_t hdr_end_pa,
                                   uint64_t text_start_pa,
                                   uint64_t text_end_pa,
                                   uint64_t ro_start_pa,
                                   uint64_t ro_end_pa,
                                   uint64_t data_start_pa,
                                   uint64_t bss_end_pa) {
  // Kernel header: RO + XN.
  if (in_range(pa, hdr_start_pa, hdr_end_pa)) {
    return ATTR_NORMAL_RO_XN;
  }
  // Text: RO + executable.
  if (in_range(pa, text_start_pa, text_end_pa)) {
    return ATTR_NORMAL_RO_RX;
  }
  // Rodata: RO + XN.
  if (in_range(pa, ro_start_pa, ro_end_pa)) {
    return ATTR_NORMAL_RO_XN;
  }
  // Data/BSS: RW + XN.
  if (in_range(pa, data_start_pa, bss_end_pa)) {
    return ATTR_NORMAL_RW_XN;
  }
  // Default for kernel pages (conservative): RW + XN.
  return ATTR_NORMAL_RW_XN;
}

// -----------------------------------------------------------------------------
// Rebuild TTBR1 with:
//   - L1[0] : low 1 GiB device window (device, RW, XN)
//   - L1[1] : 1 GiB RAM window (normal, default RW XN 2 MiB blocks)
//             with kernel image pages split to L3 and protected (W^X)
// -----------------------------------------------------------------------------

static uint64_t *build_ttbr1_wx(uint64_t *out_l0_pa) {
  (void)out_l0_pa;

  uint64_t *l0 = pt_alloc_table();
  uint64_t *l1 = pt_alloc_table();
  uint64_t *l2_ram = pt_alloc_table();

  const uint64_t l0_pa = kva_to_pa(l0);
  const uint64_t l1_pa = kva_to_pa(l1);
  const uint64_t l2_ram_pa = kva_to_pa(l2_ram);

  // Map the high-half 512 GiB region that starts at HH_PHYS_BASE.
  l0[idx_l0(HH_PHYS_BASE)] = pte_table(l1_pa);

  // L1[0]: device window (PA 0x0000_0000..0x3FFF_FFFF)
  l1[0] = pte_block(0x00000000ULL, ATTR_DEVICE_RW_XN);

  // L1[1]: RAM window (PA 0x4000_0000..0x7FFF_FFFF) -> L2 table.
  l1[1] = pte_table(l2_ram_pa);

  // Default-fill RAM region with 2 MiB blocks: RW + XN.
  for (uint64_t i = 0; i < 512; i++) {
    uint64_t pa = RAM_BASE + i * SZ_2M;
    l2_ram[i] = pte_block(pa, ATTR_NORMAL_RW_XN);
  }

  // Compute kernel segment PA ranges (linker symbols are VAs).
  const uint64_t hdr_start_pa = pa_of_kva(__kernel_header_start);
  const uint64_t hdr_end_pa   = pa_of_kva(__kernel_header_end);

  const uint64_t text_start_pa = pa_of_kva(__text_start);
  const uint64_t text_end_pa   = pa_of_kva(__text_end);

  const uint64_t ro_start_pa = pa_of_kva(__rodata_start);
  const uint64_t ro_end_pa   = pa_of_kva(__rodata_end);

  const uint64_t data_start_pa = pa_of_kva(__data_start);
  const uint64_t data_end_pa   = pa_of_kva(__data_end);
  (void)data_end_pa;

  const uint64_t bss_end_pa   = pa_of_kva(__bss_end);

  // Walk all pages that could belong to the kernel image.
  // We bound this by header start .. bss end.
  uint64_t kern_start_pa = hdr_start_pa;
  uint64_t kern_end_pa   = bss_end_pa;

  // Round to page boundaries.
  kern_start_pa &= ~(SZ_4K - 1);
  kern_end_pa = (kern_end_pa + SZ_4K - 1) & ~(SZ_4K - 1);

  for (uint64_t pa = kern_start_pa; pa < kern_end_pa; pa += SZ_4K) {
    // Only pages within the 1 GiB RAM window are handled here.
    if (pa < RAM_BASE || pa >= (RAM_BASE + ONE_GIB)) {
      continue;
    }

    const uint64_t va = va_of_pa(pa);
    const uint64_t l2i = idx_l2(va);

    uint64_t l2e = l2_ram[l2i];
    uint64_t *l3;

    if ((l2e & 0x3ULL) == DESC_BLOCK) {
      // Split this 2 MiB block into an L3 table of RW+XN pages.
      l3 = split_l2_block_to_l3(l2e, va & ~(SZ_2M - 1), ATTR_NORMAL_RW_XN);
      l2_ram[l2i] = pte_table(kva_to_pa(l3));
    } else {
      // Table already present.
      uint64_t l3_pa = l2e & 0x0000FFFFFFFFF000ULL;
      l3 = (uint64_t *)(va_of_pa(l3_pa));
    }

    const uint64_t a = attrs_for_kernel_pa(pa,
                                           hdr_start_pa, hdr_end_pa,
                                           text_start_pa, text_end_pa,
                                           ro_start_pa, ro_end_pa,
                                           data_start_pa, bss_end_pa);
    l3[idx_l3(va)] = pte_page(pa, a);
  }

  // Return L0 physical base.
  if (out_l0_pa) {
    *out_l0_pa = l0_pa;
  }
  return l0;
}

// -----------------------------------------------------------------------------
// Public entry
// -----------------------------------------------------------------------------

void mmu_init(void *boot_info) {
  (void)boot_info;
  // 1) Install kernel vectors immediately (still under the boot mapping).
  //    This prevents taking an exception through a low-VA boot VBAR after we
  //    disable TTBR0.
  __asm__ volatile("msr VBAR_EL1, %0\n\tisb" :: "r"((uint64_t)&kernel_vectors) : "memory");

  // 2) Build new TTBR1 tables with W^X and device attributes.
  uint64_t l0_pa = 0;
  (void)build_ttbr1_wx(&l0_pa);

  // 3) Program MAIR (AttrIndx 0 = Normal WBWA, 1 = Device-nGnRE).
  const uint64_t mair = 0
      | (0xFFULL << 0)   // Attr 0: Normal memory, Inner/Outer WBWA
      | (0x04ULL << 8);  // Attr 1: Device-nGnRE
  __asm__ volatile("msr MAIR_EL1, %0" :: "r"(mair) : "memory");

  // 4) Set TCR for 4 KiB granule and disable TTBR0 walks (EPD0=1).
  //    Keep values consistent with the boot TCR setup.
  const uint64_t tcr =
      (16ULL << 0) |       // T0SZ (ignored when EPD0=1)
      (16ULL << 16) |      // T1SZ
      (0ULL << 8) |        // IRGN0
      (0ULL << 10) |       // ORGN0
      (0ULL << 12) |       // SH0
      (0ULL << 14) |       // TG0 (4 KiB)
      (1ULL << 7) |        // EPD0 = 1
      (3ULL << 24) |       // IRGN1 = WBWA
      (3ULL << 26) |       // ORGN1 = WBWA
      (3ULL << 28) |       // SH1 = Inner
      (2ULL << 30) |       // TG1 = 4 KiB
      (0ULL << 22);        // A1 = 0 (ASID in TTBR0, irrelevant for now)
  __asm__ volatile("dsb ish\n\tmsr TCR_EL1, %0\n\tisb" :: "r"(tcr) : "memory");

  // 5) Switch TTBR1 to the new hierarchy.
  __asm__ volatile("dsb ishst\n\tmsr TTBR1_EL1, %0\n\tisb" :: "r"(l0_pa) : "memory");

  // 6) Force W^X in hardware (SCTLR.WXN=1). This makes any writable mapping XN.
  uint64_t sctlr;
  __asm__ volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));
  sctlr |= (1ULL << 19);  // WXN
  __asm__ volatile("msr SCTLR_EL1, %0\n\tisb" :: "r"(sctlr) : "memory");

  // 7) Invalidate TLB + I-cache to ensure new permissions are observed.
  __asm__ volatile(
      "dsb ish\n\t"
      "tlbi vmalle1is\n\t"
      "dsb ish\n\t"
      "isb\n\t"
      "ic iallu\n\t"
      "dsb ish\n\t"
      "isb" ::: "memory");

  // 8) Disable TTBR0 explicitly (belt-and-suspenders) after the new VBAR is live.
  __asm__ volatile("msr TTBR0_EL1, xzr\n\tisb" ::: "memory");
}
