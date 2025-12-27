//  start.S
//  Capaz
//
//  Phase 3: bootstrap in low PA, then transition to higher-half kernel (TTBR1).
//

.section .text._start, "ax"
.global _start
.type _start, %function

.extern __boot_stack_top
.extern boot_vectors
.extern boot_higherhalf

_start:
    // Boot stack (identity, MMU off)
    adrp x0, __boot_stack_top
    add  x0, x0, :lo12:__boot_stack_top
    mov sp, x0

    // Install a minimal boot vector table (keeps faults diagnosable during early MMU work)
    adrp x0, boot_vectors
    add  x0, x0, :lo12:boot_vectors
    msr vbar_el1, x0
    isb

    // Continue in bootstrap
    b   boot_higherhalf

1:  wfe
    b   1b


// Minimal boot vectors: spin in place.
// Must be 2KB-aligned for VBAR_EL1.
.section .text.boot, "ax"
.align 11
.global boot_vectors
.type boot_vectors, %function

.macro BOOT_VEC label
  .align 7
\label:
  b \label
  .space 128 - 4, 0
.endm

boot_vectors:
  // Current EL, SP0
  BOOT_VEC boot_sync_cur_sp0
  BOOT_VEC boot_irq_cur_sp0
  BOOT_VEC boot_fiq_cur_sp0
  BOOT_VEC boot_serror_cur_sp0

  // Current EL, SPx
  BOOT_VEC boot_sync_cur_spx
  BOOT_VEC boot_irq_cur_spx
  BOOT_VEC boot_fiq_cur_spx
  BOOT_VEC boot_serror_cur_spx

  // Lower EL, AArch64
  BOOT_VEC boot_sync_low_a64
  BOOT_VEC boot_irq_low_a64
  BOOT_VEC boot_fiq_low_a64
  BOOT_VEC boot_serror_low_a64

  // Lower EL, AArch32
  BOOT_VEC boot_sync_low_a32
  BOOT_VEC boot_irq_low_a32
  BOOT_VEC boot_fiq_low_a32
  BOOT_VEC boot_serror_low_a32
