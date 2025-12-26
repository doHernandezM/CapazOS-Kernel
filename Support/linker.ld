ENTRY(_start)

SECTIONS
{
  __phys_base    = 0x40000000;

  /* Link the main kernel at the higher-half VA base (must match vm_layout.h). */
  __kern_va_base = 0xFFFFFF8040000000;
  __kern_offset  = __kern_va_base - __phys_base;

  /* ---------------- Boot / bootstrap (identity, MMU off) ---------------- */
  . = __phys_base;

  __boot_text_start = .;
  .text.boot : ALIGN(0x1000) {
    *(.text._start)
    *(.text.boot*)
    *(.text.boot_higherhalf*)
  }
  __boot_text_end = .;

  __boot_rodata_start = .;
  .rodata.boot : ALIGN(16) {
    *(.rodata.boot*)
  }
  __boot_rodata_end = .;

  __boot_data_start = .;
  .data.boot : ALIGN(16) {
    *(.data.boot*)
  }
  __boot_data_end = .;

  .bss.boot (NOLOAD) : ALIGN(16) {
    *(.bss.boot*)
  }

  /* Boot stack (identity) */
  .bootstack (NOLOAD) : ALIGN(0x1000) {
    __boot_stack_bottom = .;
    . += 0x4000; /* 16KB boot stack */
    __boot_stack_top = .;
  }

  /* Track physical load cursor for main kernel image. */
  __phys = ALIGN(0x1000);

  /* ---------------- Main kernel (higher-half VMA, PA load) -------------- */
  . = __phys + __kern_offset;

  /* Vector table must be 2KB-aligned for VBAR_EL1. Treat as code (RX). */
  .vectors ALIGN(2048) : AT(__phys) {
    __text_start = .;
    KEEP(*(.vectors))
  }
  __phys += SIZEOF(.vectors);

  .text ALIGN(0x1000) : AT(__phys) {
    *(.text*)
    __text_end = .;
  }
  __phys += SIZEOF(.text);

  .rodata ALIGN(0x1000) : AT(__phys) {
    __rodata_start = .;
    *(.rodata .rodata.*)

    *(.swift_modhash)
    *(.swift5_*)
    *(.swift*)

    __rodata_end = .;
  }
  __phys += SIZEOF(.rodata);

  .got ALIGN(0x1000) : AT(__phys) {
    *(.got .got.*)
    *(.got.plt .igot.plt)
  }
  __phys += SIZEOF(.got);

  .data ALIGN(0x1000) : AT(__phys) {
    __data_start = .;
    *(.data .data.*)
    __data_end = .;
  }
  __phys += SIZEOF(.data);

  .bss ALIGN(0x1000) (NOLOAD) : AT(__phys) {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }
  __phys += SIZEOF(.bss);

  .pt ALIGN(0x4000) (NOLOAD) : AT(__phys) {
    __pt_base = .;
    . += 0x20000;
    __pt_end = .;
  }
  __phys += 0x20000;

  .stack ALIGN(0x1000) (NOLOAD) : AT(__phys) {
    __stack_bottom = .;
    . += 0x4000;
    __stack_top = .;
  }
  __phys += 0x4000;

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
}
