/*
 * kernel.ld - linker script for the Capaz higher‑half kernel image.
 *
 * This script links the main kernel binary independently of the boot
 * image. The kernel is mapped at a fixed higher‑half virtual base
 * (KERNEL_VA_BASE) with a physical load address chosen to reside
 * immediately after the boot image in memory. The physical base used
 * here (0x40200000) must match the packaging step that concatenates
 * the boot and kernel binaries. The boot image is responsible for
 * switching to this higher‑half mapping before transferring control
 * to the kernel entry point.
 */

/* The boot image branches to the kernel's runtime stub. */
ENTRY(crt0)

SECTIONS
{
  /* Base physical address of the boot image. Not used directly in
   * this script but provided for context. */
  __phys_base    = 0x40000000;

  /* Higher‑half virtual base. Must match vm_layout.h. */
  __kern_va_base = 0xFFFFFF8040000000;

  /* Physical base address for the kernel image. The packaging step
   * pads the boot binary to 2 MiB and places the kernel immediately
   * afterwards. Align this to 2 MiB. */
  __kernel_phys_base = 0x40200000;

  /* Offset between kernel VA and kernel PA. */
  __kern_offset  = __kern_va_base - __kernel_phys_base;

  /* Track physical load cursor for the kernel. */
  __phys = __kernel_phys_base;

  /* Place the kernel at its higher‑half VMA. */
  . = __phys + __kern_offset;

  /* Vector table must be >=2KB‑aligned for VBAR_EL1. 4KB keeps it page‑aligned
   * for page permission splits in early bootstrap. Treat as code (RX). */
  .vectors ALIGN(0x1000) : AT(__phys) {
    __text_start = .;
    KEEP(*(.vectors))
  }
  __phys += SIZEOF(.vectors);

  .text ALIGN(0x1000) : AT(__phys) {
    *(.text*)
    . = ALIGN(0x1000);
    __text_end = .;
  }
  __phys += SIZEOF(.text);

  .rodata ALIGN(0x1000) : AT(__phys) {
    __rodata_start = .;
    *(.rodata .rodata.*)
    *(.swift_modhash)
    *(.swift5_*)
    *(.swift*)
    . = ALIGN(0x1000);
    __rodata_end = .;
  }
  __phys += SIZEOF(.rodata);

  /* Writable data. Keep the GOT here so it is mapped RW+NX. */
  .data ALIGN(0x1000) : AT(__phys) {
    __data_start = .;
    *(.got .got.*)
    *(.got.plt .igot.plt)
    *(.data .data.*)
    . = ALIGN(0x1000);
    __data_end = .;
  }
  __phys += SIZEOF(.data);

  .bss ALIGN(0x1000) (NOLOAD) : AT(__phys) {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(0x1000);
    __bss_end = .;
  }
  __phys += SIZEOF(.bss);

  .pt ALIGN(0x4000) (NOLOAD) : AT(__phys) {
    __pt_base = .;
    . += 0x20000; /* 128KB page‑table arena */
    __pt_end = .;
  }
  __phys += 0x20000;

  .stack ALIGN(0x1000) (NOLOAD) : AT(__phys) {
    __stack_bottom = .;
    . += 0x4000;
    __stack_top = .;
  }
  __phys += 0x4000;

  /* End of the kernel's reserved physical image (includes NOLOAD regions). */
  PROVIDE(__kernel_phys_end = __phys);
  PROVIDE(__kernel_phys_size = (__kernel_phys_end - __kernel_phys_base));

  /* Convenience VA equivalents (useful for tooling). */
  PROVIDE(__kernel_va_end = (__kernel_phys_end + __kern_offset));
  PROVIDE(__kernel_va_size = (__kernel_va_end - __kern_va_base));

  /* --- Provide PA equivalents for bootstrap --- */
  PROVIDE(__pt_base_phys      = __pt_base      - __kern_offset);
  PROVIDE(__pt_end_phys       = __pt_end       - __kern_offset);

  PROVIDE(__stack_bottom_phys = __stack_bottom - __kern_offset);
  PROVIDE(__stack_top_phys    = __stack_top    - __kern_offset);

  PROVIDE(__text_start_phys   = __text_start   - __kern_offset);
  PROVIDE(__text_end_phys     = __text_end     - __kern_offset);

  PROVIDE(__rodata_start_phys = __rodata_start - __kern_offset);
  PROVIDE(__rodata_end_phys   = __rodata_end   - __kern_offset);

  PROVIDE(__data_start_phys   = __data_start   - __kern_offset);
  PROVIDE(__data_end_phys     = __data_end     - __kern_offset);

  PROVIDE(__bss_start_phys    = __bss_start    - __kern_offset);
  PROVIDE(__bss_end_phys      = __bss_end      - __kern_offset);

  PROVIDE(vectors_phys        = vectors        - __kern_offset);
  PROVIDE(__vectors_phys      = vectors        - __kern_offset);

  /* Physical alias for crt0 so low‑address bootstrap code can jump to it
     without forming PC‑relative references to the higher‑half VMA. */
  PROVIDE(crt0_phys           = crt0           - __kern_offset);
}