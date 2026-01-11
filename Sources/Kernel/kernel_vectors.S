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
    .extern sched_irq_exit

    .global kernel_sync_entry
    .type   kernel_sync_entry, %function
    .global kernel_irq_entry
    .type   kernel_irq_entry, %function

    // Trap frame layout (must match trap_frame_t in irq.h)
    .equ TF_GPRS_SIZE,  (32 * 8)       // x0..x30 + pad
    .equ TF_SP_OFF,     (32 * 8)       // original SP (before PUSH_GPRS)
    .equ TF_ELR_OFF,    (33 * 8)
    .equ TF_SPSR_OFF,   (34 * 8)
    .equ TF_ESR_OFF,    (35 * 8)
    .equ TF_FAR_OFF,    (36 * 8)
    .equ TF_SP_EL0_OFF, (37 * 8)
    .equ TF_SIZE,       (38 * 8)       // total, 16-byte aligned

    /* Save/restore trap frame. Layout matches trap_frame_t in irq.h. */
    .macro PUSH_GPRS
        // Keep SP 16-byte aligned (AArch64 ABI) across the call into C.
        sub sp, sp, #TF_SIZE

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

        // Save exception return/system state for M8 (IRQ-return preemption).
        // Note: ESR_EL1/FAR_EL1 are only architecturally meaningful for
        // synchronous exceptions, but we capture them here for uniformity.
        // Record the interrupted SP (pre-save). This is the stack pointer the
        // CPU was using at exception entry.
        add x9, sp, #TF_SIZE
        str x9, [sp, #TF_SP_OFF]

        mrs x9, elr_el1
        str x9, [sp, #TF_ELR_OFF]
        mrs x9, spsr_el1
        str x9, [sp, #TF_SPSR_OFF]
        mrs x9, esr_el1
        str x9, [sp, #TF_ESR_OFF]
        mrs x9, far_el1
        str x9, [sp, #TF_FAR_OFF]

        // Capture SP_EL0 for future EL0 support / debugging.
        mrs x9, sp_el0
        str x9, [sp, #TF_SP_EL0_OFF]
    .endm

    .macro POP_GPRS
        // Restore the exception return state first so an M8-style scheduler
        // can swap SP to a different saved frame before returning.
        ldr x9, [sp, #TF_ELR_OFF]
        msr elr_el1, x9
        ldr x9, [sp, #TF_SPSR_OFF]
        msr spsr_el1, x9

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

        add sp, sp, #TF_SIZE
    .endm

/* -------------------------------------------------------------------------- */
/* Synchronous exceptions: dump and park                                       */
/* -------------------------------------------------------------------------- */
kernel_sync_entry:
    PUSH_GPRS

    mrs x0, esr_el1
    mrs x1, far_el1
    mrs x2, elr_el1

    /* x0..x4 = (esr, far, elr, sp_at_fault, regs_ptr) */
    /* x0..x2 already loaded above */
    add x3, sp, #TF_SIZE     /* original SP before this handler */
    mov x4, sp              /* regs_ptr */
    bl  kernel_exception_report

1:  wfe
    b   1b
    .size kernel_sync_entry, .-kernel_sync_entry

/* -------------------------------------------------------------------------- */
/* IRQs: dispatch and return                                                   */
/* -------------------------------------------------------------------------- */
kernel_irq_entry:
    // Keep IRQs masked for the full handler path (no nesting for now).
    // Hardware sets PSTATE.I on IRQ entry, but we set it defensively.
    msr daifset, #2

    PUSH_GPRS

    /* x0 = trap_frame_t* */
    mov x0, sp
    bl  irq_dispatch

    /*
     * Phase 5: IRQ-exit scheduler hook.
     *
     * After all IRQ handlers ran, allow the scheduler to optionally switch
     * threads by returning a different trap-frame pointer. If a different
     * pointer is returned, we swap SP so the restore+ERET resumes the next
     * thread instead of the interrupted one.
     *
     * LK inspiration (conceptual): LK's ARM64 irq_exception macro calls
     * thread_preempt between IRQ handling and frame restore, achieving
     * preemption at exception-exit.
     *   lk-master/arch/arm64/exceptions.S (irq_exception macro; MIT header)
     */
    mov x0, sp
    bl  sched_irq_exit
    mov x9, sp
    cmp x0, x9
    b.eq 1f
    mov sp, x0
1:

    POP_GPRS
    isb
    eret
    .size kernel_irq_entry, .-kernel_irq_entry

    .size kernel_vectors, .-kernel_vectors
