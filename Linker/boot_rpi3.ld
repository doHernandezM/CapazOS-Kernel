/* Kernel/Linker/boot_rpi3.ld
 * rPi3 firmware loads KERNEL8.IMG at 0x00080000.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x00080000;
    PROVIDE(__boot_image_start = .);

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

    PROVIDE(__boot_image_end = .);

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
    . = . + 0x4000; /* 16 KiB */
    PROVIDE(__boot_stack_top = .);
}
