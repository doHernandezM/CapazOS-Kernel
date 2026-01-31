/* OS/Kern/Arch/aarch64/start.S
 *
 * Boot stage for Capaz.
 *
 * QEMU 'virt' follows the Linux AArch64 boot protocol and provides:
 *   x0 = physical address of the DTB (FDT blob)
 *
 * This stage:
 *   - sets up a temporary stack
 *   - enables the MMU with a minimal TTBR0+TTBR1 mapping
 *   - constructs boot_info and branches to the kernel image entry
 *
 * Fixes included:
 *   1) Correct high-half L0 index for 0xFFFF8000_.... with 48-bit VA: L0[256]
 *   2) Map UART MMIO after MMU enable by adding L1[0] 1GiB block (0x0000_0000..0x3FFF_FFFF)
 *   3) Correct 2KiB exception vector layout (16 slots * 0x80)
 *   4) Record ESR/FAR/ELR on exception (inspectable in GDB)
 *   5) Pass boot_info pointer as high-half direct-map VA (safe if kernel disables TTBR0)
 *   6) Capture DTB pointer from x0 and pass DTB (VA + size) in boot_info
 */
#include "buildinfo.h"


    .section .text._start, "ax"
    .global _start
    .type _start, %function

_start:
    /* Save QEMU-provided DTB PA from x0 before clobbering it. */
    mov     x20, x0                      /* dtb_pa */

    /* 1) Stack */
    adrp    x0, __boot_stack_top
    add     x0, x0, :lo12:__boot_stack_top
    mov     sp, x0

    /* 2) Zero boot BSS */
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

    /* 3) VBAR + enable FP/SIMD */
    adrp    x1, boot_vectors
    add     x1, x1, :lo12:boot_vectors
    msr     vbar_el1, x1
    isb

    mrs     x1, cpacr_el1
    orr     x1, x1, #(3 << 20)          /* FPEN=0b11 */
    msr     cpacr_el1, x1
    isb

    /* 4) Banner (pre-MMU; works without MMIO mappings) */
    adrp    x1, boot_banner
    add     x1, x1, :lo12:boot_banner
2:
    ldrb    w2, [x1], #1
    cbz     w2, 3f
    bl      uart_send
    b       2b
3:

    /* 5) Translation tables
     *   L0[0]   -> L1 (low VA via TTBR0)
     *   L0[256] -> L1 (0xFFFF8000_0000_0000.. via TTBR1)
     *
     *   L1[0] -> 1GiB block for device/MMIO region 0x0000_0000..0x3FFF_FFFF
     *   L1[1] -> 1GiB block for RAM         region 0x4000_0000..0x7FFF_FFFF
     */
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

    /* L1[0] device/MMIO block: VA/PA 0x0000_0000..0x3FFF_FFFF
     * AttrIndx = 1 (device), AF=1, valid block.
     * SH = 0 (non-shareable) for device.
     */
    mov     x6, xzr                      /* OA = 0x0000_0000 */
    orr     x6, x6, #(1 << 2)            /* AttrIndx=1 */
    orr     x6, x6, #(1 << 10)           /* AF=1 */
    orr     x6, x6, #1                   /* valid block */
    str     x6, [x5, #0]                 /* L1[0] */

    /* L1[1] RAM block: VA/PA 0x4000_0000..0x7FFF_FFFF
     * AttrIndx = 0 (normal), SH=3 (inner shareable), AF=1, valid block.
     */
    mov     x6, #0x40000000
    orr     x6, x6, #(3 << 8)            /* SH=3 */
    orr     x6, x6, #(1 << 10)           /* AF=1 */
    orr     x6, x6, #1                   /* valid block */
    str     x6, [x5, #8]                 /* L1[1] */

    /* Map additional RAM GiB blocks to widen early TTBR1 coverage.
     * This avoids failures if the DTB or early kernel data structures are placed
     * above the first GiB of RAM. The kernel will rebuild TTBR1 in mmu_init()
     * using DTB memory ranges. */
    mov     x9, #2                      /* start at L1[2] (0x8000_0000) */
    mov     x10, #8                     /* map through L1[8] (8GiB total from RAM_BASE) */
1:
    cmp     x9, x10
    b.gt    2f

    /* OA = (index * 1GiB) */
    mov     x6, x9
    lsl     x6, x6, #30
    orr     x6, x6, #(3 << 8)           /* SH=3 */
    orr     x6, x6, #(1 << 10)          /* AF=1 */
    orr     x6, x6, #1                  /* valid block */
    str     x6, [x5, x9, lsl #3]        /* L1[index] */

    add     x9, x9, #1
    b       1b
2:


    dsb     ishst

    /* 6) MMU on (TTBR0 + TTBR1 both active during bring-up) */

    /* MAIR:
     *  Attr0 = 0xFF (Normal WBWA)
     *  Attr1 = 0x04 (Device-nGnRE)
     */
    ldr     x6, =0x04FF
    msr     mair_el1, x6

    /* TCR: 48-bit VA for both, 4KiB granule, WBWA, inner-shareable, EPD0=0 */
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

    /* 7) boot_info + locate kernel:
     * kernel_pa = __boot_image_start + align_up((__boot_image_end-__boot_image_start), 2MiB)
     */
    adrp    x7, boot_info
    add     x7, x7, :lo12:boot_info

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

    ldr     x13, [x8, #8]               /* image_size */
    ldr     x14, [x8, #16]              /* entry_offset */

    /*
     * boot_info layout (64-bit fields):
     *   [0]  kernel_phys_base
     *   [8]  kernel_size
     *   [16] kernel_entry_offset
     *   [24] dtb_ptr (high-half VA)
     *   [32] dtb_size (bytes)
     */
    str     x8,  [x7, #0]
    str     x13, [x7, #8]
    str     x14, [x7, #16]

    /* DTB pointer/size (from x20 = dtb_pa). */
    mov     x21, xzr                    /* dtb_va */
    mov     x22, xzr                    /* dtb_size */

    cbz     x20, 5f

    /* Validate DTB magic and read totalsize (big-endian). */
    ldr     w23, [x20, #0]              /* magic */
    rev     w23, w23
    ldr     w24, =0xD00DFEED
    cmp     w23, w24
    b.ne    5f

    ldr     w25, [x20, #4]              /* totalsize */
    rev     w25, w25
    uxtw    x22, w25

    /* High-half direct-map VA for DTB: HH_PHYS_BASE + dtb_pa */
    ldr     x26, =0xFFFF800000000000    /* HH_PHYS_BASE */
    add     x21, x26, x20

5:
    str     x21, [x7, #24]
    str     x22, [x7, #32]

    /* Pass boot_info pointer as high-half direct-map VA. */
    ldr     x16, =0xFFFF800000000000    /* HH_PHYS_BASE */
    add     x0, x16, x7                 /* x0 = boot_info high VA */

    /* 8) Jump to kernel VA:
     * entry_va = HH_PHYS_BASE + kernel_pa + entry_offset
     */
    add     x16, x16, x8                /* kernel_base_va */
    add     x9,  x16, x14               /* entry_va */
    br      x9

.Lbad_kernel_header:
    wfe
    b       .Lbad_kernel_header

/* ------------------------------------------------------------------ */
/* UART send (uses low VA 0x0900_0000; OK pre-MMU; mapped post-MMU too) */
    .type uart_send, %function
uart_send:
    adrp    x3, uart_base
    add     x3, x3, :lo12:uart_base
    ldr     x3, [x3]
4:
    ldr     w4, [x3, #0x18]
    tst     w4, #(1 << 5)
    b.ne    4b
    str     w2, [x3]
    ret

/* ------------------------------------------------------------------ */
/* Data */
    .section .rodata.boot, "a"
boot_banner:
    .ascii "Boot: "
    .ascii CAPAZ_BUILD_VERSION
    .ascii "\n"
    .ascii "Platform: "
    .ascii CAPAZ_KERNEL_PLATFORM
    .ascii "\n\n"
    .byte 0



    .section .data.boot, "aw"
uart_base:
    .quad 0x09000000

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
    /* Record ESR/FAR/ELR for GDB inspection, then spin */
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
