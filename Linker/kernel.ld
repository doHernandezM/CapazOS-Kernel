/* Kernel/Linker/kernel.ld
 * Kernel linker script: higher-half VMA, physical LMA (lld-friendly).
 */

ENTRY(_kcrt0)

SECTIONS
{
  PROVIDE(RAM_BASE = 0x40000000);
  PROVIDE(HH_PHYS_4000_BASE = 0xFFFF800040000000);

  /* Defaults; build.sh overrides via --defsym */
  PROVIDE(KERNEL_PHYS_BASE = 0x40280000);
  PROVIDE(KERNEL_VA_BASE   = HH_PHYS_4000_BASE + (KERNEL_PHYS_BASE - RAM_BASE));
}

MEMORY
{
  KVIRT (rxw) : ORIGIN = KERNEL_VA_BASE,   LENGTH = 64M
  KPHYS (rxw) : ORIGIN = KERNEL_PHYS_BASE, LENGTH = 64M
}

PHDRS
{
  /*
   * Single loadable segment.
   *
   * We intentionally avoid multiple PT_LOAD segments here because lld keeps a
   * separate load-address counter per segment. When using a single AT> region,
   * that can yield overlapping LMAs (both counters start at ORIGIN(KPHYS)).
   *
   * Page protections are enforced by our own paging setup (using the section
   * boundary symbols below), so we do not rely on ELF program-header flags.
   */
  kernel PT_LOAD FLAGS(7); /* RWX (metadata only for our loader) */
}

SECTIONS
{
  . = KERNEL_VA_BASE;
  __kernel_image_start = .;

  .kernel_header :
  {
    . = ALIGN(16);
    KEEP(*(.kernel_header))
  } >KVIRT AT> KPHYS :kernel

  .text :
  {
    . = ALIGN(0x1000);
    __text_start = .;
    *(.text._kcrt0)
    *(.text*)
    __text_end = .;
  } >KVIRT AT> KPHYS :kernel

  .rodata :
  {
    . = ALIGN(0x1000);
    __rodata_start = .;
    *(.rodata*)
    __rodata_end = .;
  } >KVIRT AT> KPHYS :kernel


  .text.core :
  {
    . = ALIGN(0x1000);
    __core_text_start = .;
    *(.text.core*)
    __core_text_end = .;
  } >KVIRT AT> KPHYS :kernel

  .rodata.core :
  {
    . = ALIGN(0x1000);
    __core_rodata_start = .;
    *(.rodata.core*)
    __core_rodata_end = .;
  } >KVIRT AT> KPHYS :kernel

  .data :
  {
    . = ALIGN(0x1000);
    __data_start = .;
    *(.data*)
    __data_end = .;
  } >KVIRT AT> KPHYS :kernel


  .data.core :
  {
    . = ALIGN(0x1000);
    __core_data_start = .;
    *(.data.core*)
    __core_data_end = .;
    __kernel_loaded_end = .;
  } >KVIRT AT> KPHYS :kernel

  .bss (NOLOAD) :
  {
    . = ALIGN(16);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;


  } >KVIRT :kernel

  .bss.core (NOLOAD) :
  {
    . = ALIGN(16);
    __core_bss_start = .;
    *(.bss.core*)
    __core_bss_end = .;

    /* Page-aligned end of the kernel runtime footprint (.bss + .bss.core). */
    . = ALIGN(0x1000);
    __kernel_runtime_end = .;

    __kernel_image_end     = __kernel_loaded_end;
    __kernel_image_size    = __kernel_image_end - __kernel_image_start;
    __kernel_entry_offset  = _kcrt0 - __kernel_image_start;
  } >KVIRT :kernel

}
