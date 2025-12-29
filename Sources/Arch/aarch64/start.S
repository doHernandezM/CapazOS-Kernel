/*
 * start.S - Boot entry point and trivial UART printing
 *
 * This assembly file provides the very first instructions executed when
 * the processor starts in EL1.  It sets up a temporary stack, writes
 * a banner to the PL011 UART at 0x09000000, and then enters an
 * infinite loop.  No MMU is enabled at this stage; the boot code
 * executes in identity‑mapped physical memory.  The linker script
 * defines __boot_stack_top, which we use to initialise SP.  A very
 * simple UART transmit function is provided to avoid any C runtime
 * dependencies.
 */

    .section .text._start, "ax"
    .global _start
    .type _start, %function

/* Boot entry point */
_start:
    /* Set up a stack pointer.  The symbol __boot_stack_top is exported
       by the linker script (boot.ld) and marks the top of a 4 KiB
       bootstrap stack.  We load its address directly using an
       ADRP/ADD pair for position‑independent reference. */
    adrp    x0, __boot_stack_top
    add     x0, x0, :lo12:__boot_stack_top
    mov     sp, x0

    /* Load the address of the banner string into x1.  Again use
       ADRP/ADD to construct a PC‑relative address. */
    adrp    x1, boot_banner
    add     x1, x1, :lo12:boot_banner

1:  /* Read a byte from the banner. */
    ldrb    w2, [x1], #1
    cbz     w2, 2f           /* finished when zero byte encountered */
    bl      uart_send        /* print the character */
    b       1b

2:  /* Hang forever after printing the banner. */
    wfe
    b       2b

/*
 * Simple UART transmit routine.
 *
 * Input:
 *   w2 – byte to send
 * Clobbers:
 *   x3, w3, w4
 *
 * The PL011 UART on the QEMU virt machine is memory mapped at
 * 0x09000000.  We busy‑wait on the TXFF bit in the flag register (bit 5)
 * being clear before writing the byte to the data register.
 */
    .type uart_send, %function
uart_send:
    adrp    x3, uart_base
    add     x3, x3, :lo12:uart_base
    ldr     x3, [x3]             // x3 := 0x09000000

1:  ldr     w4, [x3, #0x18]      // w4 := UARTFR
    tst     w4, #(1 << 5)        // TXFF?
    b.ne    1b                   // loop while FIFO full

    str     w2, [x3]             // write the character
    ret

/* Data section for the banner and UART base address */
    .section .rodata.boot, "a"
boot_banner:
    .asciz "Boot stage reached\n"

    .section .data.boot, "aw"
uart_base:
    .quad 0x09000000
