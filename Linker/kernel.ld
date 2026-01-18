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
  text PT_LOAD FLAGS(5); /* R-X */
  data PT_LOAD FLAGS(6); /* RW- */
}

SECTIONS
{
  . = KERNEL_VA_BASE;
  __kernel_image_start = .;

  .kernel_header :
  {
    . = ALIGN(16);
    KEEP(*(.kernel_header))
  } >KVIRT AT>KPHYS :text

  .text :
  {
    . = ALIGN(0x1000);
    __text_start = .;
    *(.text._kcrt0)
    *(.text*)
    __text_end = .;
  } >KVIRT AT>KPHYS :text

  .rodata :
  {
    . = ALIGN(0x1000);
    __rodata_start = .;
    *(.rodata*)
    __rodata_end = .;
  } >KVIRT AT>KPHYS :text


  .text.core :
  {
    . = ALIGN(0x1000);
    __core_text_start = .;
    *(.text.core*)
    __core_text_end = .;
  } >KVIRT AT>KPHYS :text

  .rodata.core :
  {
    . = ALIGN(0x1000);
    __core_rodata_start = .;
    *(.rodata.core*)
    __core_rodata_end = .;
  } >KVIRT AT>KPHYS :text

  .data :
  {
    . = ALIGN(0x1000);
    __data_start = .;
    *(.data*)
    __data_end = .;
  } >KVIRT AT>KPHYS :data


  .data.core :
  {
    . = ALIGN(0x1000);
    __core_data_start = .;
    *(.data.core*)
    __core_data_end = .;
    __kernel_loaded_end = .;
  } >KVIRT AT>KPHYS :data

  .bss (NOLOAD) :
  {
    . = ALIGN(16);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;


  } >KVIRT :data

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
  } >KVIRT :data

}
