/*
 * Linker script for the boot image.
 *
 * This script links the boot stage at the fixed physical address
 * 0x4000_0000, allocates a small BSS region and reserves a 4 KiB stack.
 * The stack top symbol (__boot_stack_top) is exported for the assembly
 * startup code to initialise SP.  Sections are deliberately coarse at
 * this phase; fine‑grained permissions will be applied after MMU
 * enablement in later milestones.
 */

ENTRY(_start)

SECTIONS
{
    /* The boot image is loaded at 0x4000_0000 in physical memory. */
    . = 0x40000000;

    /* Text and read‑only data for the boot stage.  We include .text._start
       explicitly to ensure the first instruction is placed at the
       beginning of the file. */
    .text.boot : ALIGN(16)
    {
        *(.text._start)
        *(.text.boot)
        *(.text*)
    }

    /* Read‑only data. */
    .rodata.boot : ALIGN(16)
    {
        *(.rodata.boot)
        *(.rodata*)
    }

    /* Initialised data. */
    .data.boot : ALIGN(16)
    {
        *(.data.boot)
        *(.data*)
    }


    /* Reserve a small stack for boot code.  The assembly startup will
       initialise SP to __boot_stack_top. */
    . = ALIGN(16);
    __boot_stack_bottom = .;
    . = . + 0x1000; /* 4 KiB stack */
    __boot_stack_top = .;
}
