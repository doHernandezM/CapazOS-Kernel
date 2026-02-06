/* Loader start (virt)
 *
 * QEMU 'virt' follows the Linux AArch64 boot protocol:
 *   x0 = physical address of the DTB (FDT blob)
 *
 * This stage sets up a stack, enables the MMU with a minimal mapping, and
 * jumps into the C loader (loader_main).
 */

    .section .text._start, "ax"
    .global _start
    .type _start, %function

    .extern loader_main

_start:
    /* Save QEMU-provided DTB PA from x0. */
    mov     x20, x0                      /* dtb_pa */

    /* Stash DTB pointer for boot_dtb_ptr(). */
    adrp    x1, boot_dtb_pa
    add     x1, x1, :lo12:boot_dtb_pa
    str     x20, [x1]

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

    /* VBAR + enable FP/SIMD */
    adrp    x1, boot_vectors
    add     x1, x1, :lo12:boot_vectors
    msr     vbar_el1, x1
    isb

    mrs     x1, cpacr_el1
    orr     x1, x1, #(3 << 20)          /* FPEN=0b11 */
    msr     cpacr_el1, x1
    isb

    /* Translation tables (TTBR0 + TTBR1) */
    adrp    x4, boot_l0_table
    add     x4, x4, :lo12:boot_l0_table
    adrp    x5, boot_l1_table
    add     x5, x5, :lo12:boot_l1_table

    /* L0 table descriptor value */
    mov     x6, x5
    orr     x6, x6, #3                  /* valid table desc */
    str     x6, [x4]                    /* L0[0] */
    add     x7, x4, #2048               /* 256 * 8 */
    str     x6, [x7]                    /* L0[256] */

    /* L1[0] device/MMIO block: VA/PA 0x0000_0000..0x3FFF_FFFF */
    mov     x6, xzr                      /* OA = 0x0000_0000 */
    orr     x6, x6, #(1 << 2)            /* AttrIndx=1 */
    orr     x6, x6, #(1 << 10)           /* AF=1 */
    orr     x6, x6, #1                   /* valid block */
    str     x6, [x5, #0]                 /* L1[0] */

    /* L1[1] RAM block: VA/PA 0x4000_0000..0x7FFF_FFFF */
    mov     x6, #0x40000000
    orr     x6, x6, #(3 << 8)            /* SH=3 */
    orr     x6, x6, #(1 << 10)           /* AF=1 */
    orr     x6, x6, #1                   /* valid block */
    str     x6, [x5, #8]                 /* L1[1] */

    /* Map additional RAM GiB blocks (up to 8 GiB from RAM_BASE). */
    mov     x9, #2
    mov     x10, #8
2:
    cmp     x9, x10
    b.gt    3f
    mov     x6, x9
    lsl     x6, x6, #30
    orr     x6, x6, #(3 << 8)
    orr     x6, x6, #(1 << 10)
    orr     x6, x6, #1
    str     x6, [x5, x9, lsl #3]
    add     x9, x9, #1
    b       2b
3:

    dsb     ishst

    /* MAIR */
    ldr     x6, =0x04FF
    msr     mair_el1, x6

    /* TCR: 48-bit VA, 4KiB granule */
    ldr     x6, =0xB5103510
    msr     tcr_el1, x6
    isb

    msr     ttbr0_el1, x4
    msr     ttbr1_el1, x4
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

    /* Call loader_main(dtb_pa). */
    mov     x0, x20
    bl      loader_main

1:  wfe
    b       1b

/* ------------------------------------------------------------------ */
/* Data */
    .section .data.boot, "aw"
uart_base:
    .quad 0x09000000

    .align 3
    .global boot_dtb_pa
boot_dtb_pa:
    .quad 0

/* ------------------------------------------------------------------ */
/* Boot BSS */
    .section .bss.boot, "aw", %nobits
    .align 12
boot_l0_table:
    .skip 4096
    .align 12
boot_l1_table:
    .skip 4096
    .align 3
    .global boot_info
boot_info:
    .skip 40

    .align 3
boot_last_esr:
    .skip 8
boot_last_far:
    .skip 8
boot_last_elr:
    .skip 8

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
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception
    VEC boot_exception

boot_exception:
    mrs     x0, esr_el1
    mrs     x1, far_el1
    mrs     x2, elr_el1

    adrp    x3, boot_last_esr
    add     x3, x3, :lo12:boot_last_esr
    str     x0, [x3]

    adrp    x3, boot_last_far
    add     x3, x3, :lo12:boot_last_far
    str     x1, [x3]

    adrp    x3, boot_last_elr
    add     x3, x3, :lo12:boot_last_elr
    str     x2, [x3]

1:  wfe
    b       1b
