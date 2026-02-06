/* OS/Kern/Arch/aarch64/start_rpi3.S
 *
 * rPi3 early boot path (QEMU raspi3b).
 *
 * Boot flow:
 *  - Drop to EL1 if needed
 *  - Set a temporary stack
 *  - Install minimal exception vectors
 *  - Initialize PL011 UART
 *  - Print a banner
 *  - Enable identity-mapped MMU setup
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

    /* MMU on (identity map for early bring-up) */
    bl      mmu_init_identity

    adrp    x1, boot_banner_mmu
    add     x1, x1, :lo12:boot_banner_mmu
4:
    ldrb    w2, [x1], #1
    cbz     w2, 5f
    bl      uart_send
    b       4b
5:

    adrp    x1, boot_banner_handoff
    add     x1, x1, :lo12:boot_banner_handoff
6:
    ldrb    w2, [x1], #1
    cbz     w2, 7f
    bl      uart_send
    b       6b
7:

    /* Handoff stub into kernel image (identity-mapped, no high-half) */
    bl      kernel_handoff_stub

    /* Park CPU if handoff does not proceed. */
.Lhalt:
    wfe
    b       .Lhalt

/* ------------------------------------------------------------------ */
/* rPi3 PL011 UART init (baud 115200 @ 48MHz).
 * QEMU raspi3b doesn't require GPIO pin muxing, so skip GPIO setup here.
 */
    .type uart_init_pl011, %function
uart_init_pl011:
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

/* ------------------------------------------------------------------ */
/* Kernel handoff stub (identity-mapped) */
    .type kernel_handoff_stub, %function
kernel_handoff_stub:
    /* kernel_pa = __boot_image_start + align_up((__boot_image_end-__boot_image_start), 2MiB) */
    adrp    x18, __boot_image_start
    add     x18, x18, :lo12:__boot_image_start
    adrp    x19, __boot_image_end
    add     x19, x19, :lo12:__boot_image_end

    sub     x8, x19, x18                /* boot_size */
    ldr     x10, =0x1fffff              /* 2MiB-1 */
    add     x8, x8, x10
    bic     x8, x8, x10                 /* boot_padded */
    add     x8, x18, x8                 /* kernel_pa */

    /* Read kernel header at kernel_pa */
    ldr     w11, [x8, #0]
    ldr     w12, =0x474D494B            /* "KIMG" */
    cmp     w11, w12
    b.ne    .Lbad_kernel_header

    ldr     x14, [x8, #16]              /* entry_offset */
    add     x9,  x8, x14                /* entry_pa (identity-mapped) */

    /* Pass boot_info pointer (identity) */
    adrp    x7, boot_info
    add     x7, x7, :lo12:boot_info

    str     x8,  [x7, #0]               /* kernel_phys_base */
    ldr     x13, [x8, #8]               /* image_size */
    str     x13, [x7, #8]               /* kernel_size */
    str     x14, [x7, #16]              /* kernel_entry_offset */
    mov     x21, xzr
    mov     x22, xzr
    str     x21, [x7, #24]              /* dtb_ptr (unused) */
    str     x22, [x7, #32]              /* dtb_size */

    mov     x0, x7
    br      x9

short_delay:
    mov     w2, #150
1:
    subs    w2, w2, #1
    b.ne    1b
    ret

/* ------------------------------------------------------------------ */
/* Minimal MMU setup (identity map 1GiB with device window for peripherals) */
    .type mmu_init_identity, %function
mmu_init_identity:
    /* L0 -> L1 */
    adrp    x4, boot_l0_table
    add     x4, x4, :lo12:boot_l0_table
    adrp    x5, boot_l1_table
    add     x5, x5, :lo12:boot_l1_table
    adrp    x6, boot_l2_table
    add     x6, x6, :lo12:boot_l2_table

    mov     x7, x5
    orr     x7, x7, #3                  /* valid table desc */
    str     x7, [x4]                    /* L0[0] */

    mov     x7, x6
    orr     x7, x7, #3
    str     x7, [x5]                    /* L1[0] -> L2 */

    /* L2: map 0x00000000..0x3FFFFFFF in 2MiB blocks */
    mov     x8, #0                      /* index */
    ldr     x10, =0x3F000000            /* peripheral base */
6:
    cmp     x8, #512
    b.eq    7f

    /* base = index * 2MiB */
    mov     x12, x8
    lsl     x12, x12, #21

    /* descriptor */
    mov     x13, x12                    /* OA */

    /* Choose device vs normal based on base >= 0x3F000000 */
    cmp     x12, x10
    b.hs    .Lmmu_device

    /* Normal: AttrIndx=0, SH=3, AF=1, block */
    orr     x13, x13, #(3 << 8)         /* SH=3 */
    orr     x13, x13, #(1 << 10)        /* AF=1 */
    orr     x13, x13, #1                /* valid block */
    b       .Lmmu_store

.Lmmu_device:
    /* Device: AttrIndx=1, SH=0, AF=1, block */
    orr     x13, x13, #(1 << 2)         /* AttrIndx=1 */
    orr     x13, x13, #(1 << 10)        /* AF=1 */
    orr     x13, x13, #1                /* valid block */

.Lmmu_store:
    str     x13, [x6, x8, lsl #3]
    add     x8, x8, #1
    b       6b
7:

    dsb     ishst

    /* MAIR:
     *  Attr0 = 0xFF (Normal WBWA)
     *  Attr1 = 0x04 (Device-nGnRE)
     */
    ldr     x6, =0x04FF
    msr     mair_el1, x6

    /* TCR: 48-bit VA, 4KiB granule, EPD1=1 (disable TTBR1) */
    ldr     x6, =0x0000000015103510
    orr     x6, x6, #(1 << 23)          /* EPD1 */
    msr     tcr_el1, x6
    isb

    msr     ttbr0_el1, x4
    isb

    tlbi    vmalle1is
    dsb     ish
    isb

    mrs     x6, sctlr_el1
    orr     x6, x6, #1                  /* M */
    orr     x6, x6, #(1 << 2)           /* C */
    orr     x6, x6, #(1 << 12)          /* I */
    msr     sctlr_el1, x6
    isb

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
boot_banner_mmu:
    .asciz "CapazOS rPi3 MMU on\n"
boot_banner_handoff:
    .asciz "CapazOS rPi3 handoff\n"

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
    .align 12
boot_l0_table:
    .skip 4096
    .align 12
boot_l1_table:
    .skip 4096
    .align 12
boot_l2_table:
    .skip 4096
    .align 3
boot_info:
    .skip 40
