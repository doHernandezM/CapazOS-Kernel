// OS/Kern/Arch/aarch64/context_switch.S
// Cooperative context switching + initial thread entry trampoline.
//
// ctx_switch ABI contract (defined in Sources/Kernel/thread.h):
//   typedef struct ctx {
//     uint64_t x19..x30; // x30 = LR
//     uint64_t sp;
//   } ctx_t;
//
//   void ctx_switch(ctx_t *old, ctx_t *new);
//     - stores x19-x30 and SP to *old
//     - loads  SP and x19-x30 from *new
//     - ret: returns into the new thread's x30/LR

.text
.align  2
.global ctx_switch
.type   ctx_switch, %function
// void ctx_switch(ctx_t *old, ctx_t *new);
ctx_switch:
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x19, sp
    str     x19, [x0, #96]

    ldr     x19, [x1, #96]
    mov     sp, x19

    ldp     x19, x20, [x1, #0]
    ldp     x21, x22, [x1, #16]
    ldp     x23, x24, [x1, #32]
    ldp     x25, x26, [x1, #48]
    ldp     x27, x28, [x1, #64]
    ldp     x29, x30, [x1, #80]
    ret
.size ctx_switch, .-ctx_switch

// Initial thread entry trampoline.
// A freshly created thread sets:
//   ctx.x19 = entry
//   ctx.x20 = arg
//   ctx.x30 = &thread_start
// so that the first ret after ctx_switch lands here.
.global thread_start
.type thread_start, %function
thread_start:
    // A brand-new thread is entered via ctx_switch() while IRQs are masked
    // (the yielding thread disables IRQs around the switch boundary).
    // Unlike a resumed thread, we are not returning to yield(), so clear
    // the IRQ mask bit here to avoid running forever with IRQs disabled.
    msr     daifclr, #2
    isb
    mov     x0, x19   // entry
    mov     x1, x20   // arg
    bl      thread_trampoline
    // Should never return.
    brk     #0
1:  b       1b
.size thread_start, .-thread_start
