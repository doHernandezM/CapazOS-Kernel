ENTRY(_start)

SECTIONS
{
  /* QEMU virt loads the kernel at 0x4000_0000 by convention.
     Keeping VA==PA for Phase 2 minimizes churn while we harden permissions. */
  . = 0x40000000;

  /* Vector table must be 2KB-aligned for VBAR_EL1. Treat vectors as code (RX). */
  .vectors : ALIGN(2048) {
    __text_start = .;
    KEEP(*(.vectors))
  }

  /* Page-align .text to make RX mapping clean. */
  .text : ALIGN(0x1000) {
    *(.text._start)
    *(.text*)
    __text_end = .;
  }

  .rodata : ALIGN(0x1000) {
    __rodata_start = .;
    *(.rodata .rodata.*)
    __rodata_end = .;
  }

  .data : ALIGN(0x1000) {
    __data_start = .;
    *(.data .data.*)
    __data_end = .;
  }

  .bss : ALIGN(0x1000) {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }

  /* ---- Early MMU resources (reserved, not "somewhere in RAM") ---- */

  /* Page tables: 16KB-aligned region. Size is conservative; adjust as needed.
     Must be mapped RW+NX. */
  .pt (NOLOAD) : ALIGN(0x4000) {
    __pt_base = .;
    . += 0x20000;     /* 128KB reserved for page tables */
    __pt_end = .;
  }

  /* Early kernel stack: 16KB. We will leave the first 4KB unmapped as a guard. */
  .stack (NOLOAD) : ALIGN(0x1000) {
    __stack_bottom = .;
    . += 0x4000;
    __stack_top = .;
  }
}
