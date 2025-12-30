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

    /* Align the table on a 2KiB boundary.  Each entry occupies
     * 0x80 bytes (32 instructions) as required by the AArch64
     * architecture.  There are 16 entries covering the various
     * exception classes and stack pointers (SP0/SPx, current/lower
     * EL, AArch64/AArch32).
     */
    .align 11
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

    /* Simple exception handler.  At this early stage we do not
     * attempt to print diagnostics; instead we enter an
     * infinite loop.  The handler is marked with type
     * function so that it may be called or branched to.
     */
    .global kernel_exception
    .type kernel_exception, %function
kernel_exception:
1:  wfe
    b   1b
    .size kernel_exception, .-kernel_exception

    .size kernel_vectors, .-kernel_vectors