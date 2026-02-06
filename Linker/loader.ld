/* Linker/loader.ld
 *
 * Loader linker script for QEMU virt. The loader is a single image loaded
 * by QEMU at 0x40080000.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x40080000;
    PROVIDE(__loader_image_start = .);

    .text.boot : ALIGN(16)
    {
        *(.text._start)
        *(.text.boot)
        *(.text*)
    }

    .rodata.boot : ALIGN(16)
    {
        *(.rodata.boot)
        *(.rodata*)
    }

    .data.boot : ALIGN(16)
    {
        *(.data.boot)
        *(.data*)
    }

    PROVIDE(__loader_image_end = .);

    .bss.boot (NOLOAD) : ALIGN(16)
    {
        PROVIDE(__bss_boot_start = .);
        *(.bss.boot)
        *(.bss*)
        *(COMMON)
        PROVIDE(__bss_boot_end = .);
    }

    . = ALIGN(16);
    PROVIDE(__boot_stack_bottom = .);
    . = . + 0x2000; /* 8 KiB stack */
    PROVIDE(__boot_stack_top = .);
}
