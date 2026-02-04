/* OS/Kern/Arch/aarch64/start_rpi3.S
 *
 * rPi3 early boot stage (QEMU raspi3b).
 *
 * Goals for Milestone 1:
 *  - Drop to EL1 if needed
 *  - Set a temporary stack
 *  - Install minimal exception vectors
 *  - Initialize PL011 UART
 *  - Print a banner
 *  - No MMU (identity mapping)
 */

#include "buildinfo.h"

    .section .text._start, "ax"
    .global _start
    .type _start, %function

_start:
    /* Detect current EL */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #1
    beq     .Lel1_entry
    cmp     x0, #2
    beq     .Lel2_to_el1
    b       .Lhalt

.Lel2_to_el1:
    /* Set EL1 stack pointer */
    adrp    x1, __boot_stack_top
    add     x1, x1, :lo12:__boot_stack_top
    msr     sp_el1, x1

    /* Ensure EL1 is AArch64 */
    mov     x2, #(1 << 31)             /* HCR_EL2.RW = 1 */
    msr     hcr_el2, x2

    /* Mask interrupts on entry to EL1h */
    mov     x2, #0x3C5                 /* DAIF masked, EL1h */
    msr     spsr_el2, x2

    adr     x2, .Lel1_entry
    msr     elr_el2, x2
    eret

.Lel1_entry:
    /* Stack */
    adrp    x0, __boot_stack_top
    add     x0, x0, :lo12:__boot_stack_top
    mov     sp, x0

    /* Zero boot BSS */
    adrp    x1, __bss_boot_start
    add     x1, x1, :lo12:__bss_boot_start
    adrp    x2, __bss_boot_end
    add     x2, x2, :lo12:__bss_boot_end
    mov     x3, xzr
0:
    cmp     x1, x2
    b.hs    1f
    str     x3, [x1], #8
    b       0b
1:

    /* Install vectors */
    adrp    x1, boot_vectors
    add     x1, x1, :lo12:boot_vectors
    msr     vbar_el1, x1
    isb

    /* Enable FP/SIMD */
    mrs     x1, cpacr_el1
    orr     x1, x1, #(3 << 20)
    msr     cpacr_el1, x1
    isb

    /* UART init + banner */
    bl      uart_init_pl011

    adrp    x1, boot_banner
    add     x1, x1, :lo12:boot_banner
2:
    ldrb    w2, [x1], #1
    cbz     w2, 3f
    bl      uart_send
    b       2b
3:

    /* Park until next milestone wiring */
.Lhalt:
    wfe
    b       .Lhalt

/* ------------------------------------------------------------------ */
/* rPi3 PL011 UART init (GPIO14/15 alt0 + baud 115200 @ 48MHz) */
    .type uart_init_pl011, %function
uart_init_pl011:
    /* GPIO base = 0x3F200000 */
    ldr     x0, =0x3F200000
    /* GPFSEL1: set GPIO14/15 to alt0 (100) */
    ldr     w1, [x0, #0x04]
    bic     w1, w1, #(7 << 12)
    bic     w1, w1, #(7 << 15)
    orr     w1, w1, #(4 << 12)
    orr     w1, w1, #(4 << 15)
    str     w1, [x0, #0x04]

    /* Disable pull-ups/downs */
    mov     w1, #0
    str     w1, [x0, #0x94]            /* GPPUD */
    bl      short_delay
    mov     w1, #(1 << 14) | (1 << 15)
    str     w1, [x0, #0x98]            /* GPPUDCLK0 */
    bl      short_delay
    mov     w1, #0
    str     w1, [x0, #0x98]

    /* UART0 base = 0x3F201000 */
    ldr     x0, =0x3F201000

    /* Disable UART */
    mov     w1, #0
    str     w1, [x0, #0x30]            /* UARTCR */

    /* Clear interrupts */
    mov     w1, #0x7FF
    str     w1, [x0, #0x44]            /* UARTICR */

    /* Baud rate 115200 @ 48MHz: IBRD=26, FBRD=3 */
    mov     w1, #26
    str     w1, [x0, #0x24]            /* UARTIBRD */
    mov     w1, #3
    str     w1, [x0, #0x28]            /* UARTFBRD */

    /* 8N1, FIFO enabled */
    mov     w1, #(3 << 5) | (1 << 4)
    str     w1, [x0, #0x2C]            /* UARTLCRH */

    /* Enable UART, TX, RX */
    mov     w1, #(1 << 0) | (1 << 8) | (1 << 9)
    str     w1, [x0, #0x30]            /* UARTCR */
    ret

short_delay:
    mov     w2, #150
1:
    subs    w2, w2, #1
    b.ne    1b
    ret

/* ------------------------------------------------------------------ */
/* UART send (polling) */
    .type uart_send, %function
uart_send:
    ldr     x3, =0x3F201000
4:
    ldr     w4, [x3, #0x18]            /* UARTFR */
    tst     w4, #(1 << 5)              /* TXFF */
    b.ne    4b
    str     w2, [x3, #0x00]
    ret

/* ------------------------------------------------------------------ */
/* Data */
    .section .rodata.boot, "a"
boot_banner:
    .asciz "CapazOS rPi3 (QEMU) early boot\n"

/* ------------------------------------------------------------------ */
/* Exception vectors (2 KiB, 16 slots * 0x80) */
    .section .text.boot, "ax"
    .align 11
    .global boot_vectors

    .macro VEC target
        b \target
        .space (0x80 - 4), 0
    .endm

boot_vectors:
    /* Current EL with SP0 */
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    /* Current EL with SPx */
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    /* Lower EL using AArch64 */
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    /* Lower EL using AArch32 */
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception

boot_exception:
    wfe
    b boot_exception

/* ------------------------------------------------------------------ */
/* Boot BSS */
    .section .bss.boot, "aw", %nobits
    .align 3
    .space 8
