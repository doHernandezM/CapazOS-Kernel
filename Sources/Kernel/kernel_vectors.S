/*
 * kernel_vectors.S
 *
 * Exception vector table for the Capaz kernel.  After the boot
 * stage has installed its own vector table and brought up basic
 * paging, the kernel replaces VBAR_EL1 with the address of this
 * table.  Each entry simply branches to `kernel_exception`,
 * which currently spins indefinitely.  In future milestones this
 * handler can be extended to dump ESR_EL1, FAR_EL1 and other
 * diagnostic information over the UART.
 */

    .section .text.kernel_vectors, "ax"
    .global kernel_vectors
    .type kernel_vectors, %function

    /* Export the vector-table range for the MMU builder. */
    .global __kernel_vectors_start
    .global __kernel_vectors_end

    /* Align the table on a 2KiB boundary.  Each entry occupies
     * 0x80 bytes (32 instructions) as required by the AArch64
     * architecture.  There are 16 entries covering the various
     * exception classes and stack pointers (SP0/SPx, current/lower
     * EL, AArch64/AArch32).
     */
    .align 11
__kernel_vectors_start:
kernel_vectors:
    /* Macro to generate a single vector.  It emits an immediate
     * branch to the given label and pads the remainder of the
     * 0x80‑byte slot with zeros.  The branch keeps the table
     * position independent.  Note: `.space` does not set
     * alignment so the overall table alignment is handled above.
     */
    .macro VEC label
        b \label
        .space (0x80 - 4), 0
    .endm

    /* 16 vectors for each combination of exception level and
     * stack pointer.  All of them branch to the same simple
     * handler for now.  Future work can assign different
     * handlers to categories as needed.
     */
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception
	VEC kernel_exception

	/* End of the 2KiB vector table (16 entries * 0x80 bytes). */
__kernel_vectors_end:
    /*
     * Exception handler for bring-up.
     *
     * Saves GPR state and prints ESR_EL1/FAR_EL1/ELR_EL1 + registers over UART.
     */
    .extern kernel_exception_report
    .global kernel_exception
    .type kernel_exception, %function
kernel_exception:
    /* Save 31 GPRs (x0-x30) as 32*8 bytes (pad to 16B alignment). */
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

    /* x0..x4 = (esr, far, elr, sp_at_fault, regs_ptr) */
    mrs x0, esr_el1
    mrs x1, far_el1
    mrs x2, elr_el1
    add x3, sp, #(32 * 8)   /* original SP before this handler */
    mov x4, sp              /* regs_ptr */
    bl  kernel_exception_report

1:  wfe
    b   1b
    .size kernel_exception, .-kernel_exception

    .size kernel_vectors, .-kernel_vectors