/* Kernel/Linker/boot.ld
 *
 * QEMU virt (-machine virt -kernel <img>) loads the image at 0x40080000.
 * This linker script must match that load address.
 */

ENTRY(_start)

SECTIONS
{
    /* QEMU virt kernel load address */
    . = 0x40080000;
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

    /* End of bytes actually present in boot.bin (loadable). */
    PROVIDE(__boot_image_end = .);

    .bss.boot (NOLOAD) : ALIGN(16)
    {
        PROVIDE(__bss_boot_start = .);
        *(.bss.boot)
        *(.bss*)
        *(COMMON)
        PROVIDE(__bss_boot_end = .);
    }

    /* Reserve boot stack (not in boot.bin). */
    . = ALIGN(16);
    PROVIDE(__boot_stack_bottom = .);
    . = . + 0x1000; /* 4 KiB */
    PROVIDE(__boot_stack_top = .);
}
