/*
 * kernel_vectors.S
 *
 * Exception vector table for the Capaz kernel.
 *
 * M6: Split synchronous exceptions from IRQ handling:
 *   - kernel_sync_entry: dump state via kernel_exception_report() and park.
 *   - kernel_irq_entry : call irq_dispatch() in C and return via eret.
 */

    .section .text.kernel_vectors, "ax"
    .global kernel_vectors
    .type kernel_vectors, %function

    /* Export the vector-table range for the MMU builder. */
    .global __kernel_vectors_start
    .global __kernel_vectors_end

    /* Align the table on a 2KiB boundary. Each entry occupies 0x80 bytes. */
    .align 11
__kernel_vectors_start:
kernel_vectors:

    .macro VEC label
        b \label
        .space (0x80 - 4), 0
    .endm

    /* AArch64 vector layout (16 entries):
     *  0-3:  Current EL using SP0  (sync/irq/fiq/serr)
     *  4-7:  Current EL using SPx  (sync/irq/fiq/serr)
     *  8-11: Lower EL AArch64      (sync/irq/fiq/serr)
     * 12-15: Lower EL AArch32      (sync/irq/fiq/serr)
     *
     * We route:
     *  - Current EL sync -> kernel_sync_entry
     *  - Current EL irq  -> kernel_irq_entry
     *  - FIQ/SERR and all Lower-EL vectors -> kernel_sync_entry (no EL0 yet)
     */

    /* Current EL, SP0 */
    VEC kernel_sync_entry  /* sync_current_sp0 */
    VEC kernel_irq_entry   /* irq_current_sp0  */
    VEC kernel_sync_entry  /* fiq_current_sp0  */
    VEC kernel_sync_entry  /* serr_current_sp0 */

    /* Current EL, SPx */
    VEC kernel_sync_entry  /* sync_current_spx */
    VEC kernel_irq_entry   /* irq_current_spx  */
    VEC kernel_sync_entry  /* fiq_current_spx  */
    VEC kernel_sync_entry  /* serr_current_spx */

    /* Lower EL AArch64 (temporarily route all to sync path) */
    VEC kernel_sync_entry
    VEC kernel_sync_entry
    VEC kernel_sync_entry
    VEC kernel_sync_entry

    /* Lower EL AArch32 (temporarily route all to sync path) */
    VEC kernel_sync_entry
    VEC kernel_sync_entry
    VEC kernel_sync_entry
    VEC kernel_sync_entry

__kernel_vectors_end:

    .extern kernel_exception_report
    .extern irq_dispatch

    .global kernel_sync_entry
    .type   kernel_sync_entry, %function
    .global kernel_irq_entry
    .type   kernel_irq_entry, %function

    /* Save/restore GPRs. Layout matches trap_frame_t in irq.h:
     *   sp[0..30]  = x0..x30
     *   sp[31]     = padding
     * Total: 32*8 bytes, 16B aligned.
     */
    .macro PUSH_GPRS
        sub sp, sp, #(32 * 8)

        stp x0,  x1,  [sp, #(0 * 16)]
        stp x2,  x3,  [sp, #(1 * 16)]
        stp x4,  x5,  [sp, #(2 * 16)]
        stp x6,  x7,  [sp, #(3 * 16)]
        stp x8,  x9,  [sp, #(4 * 16)]
        stp x10, x11, [sp, #(5 * 16)]
        stp x12, x13, [sp, #(6 * 16)]
        stp x14, x15, [sp, #(7 * 16)]
        stp x16, x17, [sp, #(8 * 16)]
        stp x18, x19, [sp, #(9 * 16)]
        stp x20, x21, [sp, #(10 * 16)]
        stp x22, x23, [sp, #(11 * 16)]
        stp x24, x25, [sp, #(12 * 16)]
        stp x26, x27, [sp, #(13 * 16)]
        stp x28, x29, [sp, #(14 * 16)]
        str x30,       [sp, #(15 * 16)]
    .endm

    .macro POP_GPRS
        ldp x0,  x1,  [sp, #(0 * 16)]
        ldp x2,  x3,  [sp, #(1 * 16)]
        ldp x4,  x5,  [sp, #(2 * 16)]
        ldp x6,  x7,  [sp, #(3 * 16)]
        ldp x8,  x9,  [sp, #(4 * 16)]
        ldp x10, x11, [sp, #(5 * 16)]
        ldp x12, x13, [sp, #(6 * 16)]
        ldp x14, x15, [sp, #(7 * 16)]
        ldp x16, x17, [sp, #(8 * 16)]
        ldp x18, x19, [sp, #(9 * 16)]
        ldp x20, x21, [sp, #(10 * 16)]
        ldp x22, x23, [sp, #(11 * 16)]
        ldp x24, x25, [sp, #(12 * 16)]
        ldp x26, x27, [sp, #(13 * 16)]
        ldp x28, x29, [sp, #(14 * 16)]
        ldr x30,       [sp, #(15 * 16)]

        add sp, sp, #(32 * 8)
    .endm

/* -------------------------------------------------------------------------- */
/* Synchronous exceptions: dump and park                                       */
/* -------------------------------------------------------------------------- */
kernel_sync_entry:
    PUSH_GPRS

    /* x0..x4 = (esr, far, elr, sp_at_fault, regs_ptr) */
    mrs x0, esr_el1
    mrs x1, far_el1
    mrs x2, elr_el1
    add x3, sp, #(32 * 8)   /* original SP before this handler */
    mov x4, sp              /* regs_ptr */
    bl  kernel_exception_report

1:  wfe
    b   1b
    .size kernel_sync_entry, .-kernel_sync_entry

/* -------------------------------------------------------------------------- */
/* IRQs: dispatch and return                                                   */
/* -------------------------------------------------------------------------- */
kernel_irq_entry:
    PUSH_GPRS

    /* x0 = trap_frame_t* */
    mov x0, sp
    bl  irq_dispatch

    POP_GPRS
    eret
    .size kernel_irq_entry, .-kernel_irq_entry

    .size kernel_vectors, .-kernel_vectors
