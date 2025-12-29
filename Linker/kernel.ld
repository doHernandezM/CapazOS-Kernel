/*
 * Linker script for the stub kernel image.
 *
 * For now the kernel is placed at 0x4020_0000, i.e. 2 MiB above the
 * boot image, but the boot stage does not yet jump here.  The entry
 * point is `kmain`.  As the kernel evolves this script will grow to
 * export section boundaries for the runtime initialisation code.
 */

ENTRY(kmain)

SECTIONS
{
    /* Load kernel 2 MiB above the boot image (0x4000_0000 + 0x200000). */
    . = 0x40200000;

    .text : ALIGN(16)
    {
        *(.text*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata*)
    }

    .data : ALIGN(16)
    {
        *(.data*)
    }

}
