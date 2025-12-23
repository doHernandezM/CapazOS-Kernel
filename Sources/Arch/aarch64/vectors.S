// vectors.S
// OSpost
//
// AArch64 EL1 vector table + C trampoline.
// Phase 1: faithful frame snapshot + typed trap policy entry.
// ABI must match exceptions.h.

.section .vectors, "ax"
.align 11                 // 2KB alignment required for VBAR_EL1
.global vectors
.type vectors, %function

// ---- ABI constants (must match exceptions.h) ----
.equ EXC_FRAME_SIZE,     288
.equ EXC_STUB_SCRATCH,   16           // bytes reserved by STUB (saved x30)

.equ EXC_OFF_X0,         0
.equ EXC_OFF_X30,        (30 * 8)     // 240

.equ EXC_OFF_ESR_EL1,    (31 * 8)     // 248
.equ EXC_OFF_ELR_EL1,    (32 * 8)     // 256
.equ EXC_OFF_SPSR_EL1,   (33 * 8)     // 264
.equ EXC_OFF_FAR_EL1,    (34 * 8)     // 272

// Each vector entry is 128 bytes (architectural requirement).
// Phase 1 approach: avoid clobbering trapped GPRs.
//
// We cannot write type/origin into x1/x2 in the stub without corrupting the trapped state.
// Instead, each stub:
//   - saves the trapped x30 (LR) into a small scratch area on the current SP
//   - branches-with-link (BL) to vector_common, making x30 a unique return address inside this slot
//
// vector_common then:
//   - saves all GPRs (including the temporary x30 return address)
//   - computes (type, origin) from that return address offset within the vector table
//   - overwrites the saved x30 slot in the frame with the real trapped LR from scratch
//   - calls exception_dispatch(frame, type, origin)
.macro STUB label
  .align 7
\label:
  sub sp, sp, #EXC_STUB_SCRATCH
  str x30, [sp, #0]            // preserve trapped LR (x30)
  bl  vector_common            // x30 := return address (identifies which stub)
  .space 128 - (3*4), 0
.endm

vectors:
  // Current EL, SP0
  STUB sync_cur_sp0
  STUB irq_cur_sp0
  STUB fiq_cur_sp0
  STUB serror_cur_sp0

  // Current EL, SPx
  STUB sync_cur_spx
  STUB irq_cur_spx
  STUB fiq_cur_spx
  STUB serror_cur_spx

  // Lower EL, AArch64
  STUB sync_low_a64
  STUB irq_low_a64
  STUB fiq_low_a64
  STUB serror_low_a64

  // Lower EL, AArch32 (stubbed but present)
  STUB sync_low_a32
  STUB irq_low_a32
  STUB fiq_low_a32
  STUB serror_low_a32

.align 4
.global vector_common
.type vector_common, %function
.extern exception_dispatch

// exception_dispatch returns:
//   x0 = EXC_ACTION_PANIC => halt
//   x0 = EXC_ACTION_RESUME => restore & ERET
//   x0 = EXC_ACTION_KILL => placeholder: halt (until tasks exist)
vector_common:
  // Allocate frame (16-byte aligned size)
  sub sp, sp, #EXC_FRAME_SIZE

  // Save GPRs x0-x29 as pairs
  stp x0,  x1,  [sp, #(0*16)]
  stp x2,  x3,  [sp, #(1*16)]
  stp x4,  x5,  [sp, #(2*16)]
  stp x6,  x7,  [sp, #(3*16)]
  stp x8,  x9,  [sp, #(4*16)]
  stp x10, x11, [sp, #(5*16)]
  stp x12, x13, [sp, #(6*16)]
  stp x14, x15, [sp, #(7*16)]
  stp x16, x17, [sp, #(8*16)]
  stp x18, x19, [sp, #(9*16)]
  stp x20, x21, [sp, #(10*16)]
  stp x22, x23, [sp, #(11*16)]
  stp x24, x25, [sp, #(12*16)]
  stp x26, x27, [sp, #(13*16)]
  stp x28, x29, [sp, #(14*16)]
  str x30,       [sp, #(15*16)]        // temporary: return address from BL

  // Capture sysregs (safe to clobber x3-x6 now; originals are saved)
  mrs x3, esr_el1
  mrs x4, elr_el1
  mrs x5, spsr_el1
  mrs x6, far_el1

  // Store sysregs into frame
  str x3, [sp, #EXC_OFF_ESR_EL1]
  str x4, [sp, #EXC_OFF_ELR_EL1]
  str x5, [sp, #EXC_OFF_SPSR_EL1]
  str x6, [sp, #EXC_OFF_FAR_EL1]

  // --- Derive (type, origin) from the BL return address (in x30) ---
  // x30 points inside the 128-byte stub slot. Index = (x30 - &vectors) >> 7
  adrp x7, vectors
  add  x7, x7, :lo12:vectors
  sub  x7, x30, x7
  lsr  x7, x7, #7          // index 0..15
  and  x1, x7, #3          // type = index % 4
  lsr  x2, x7, #2          // origin = index / 4

  // --- Fix up saved x30 in the frame to be the trapped LR ---
  // The stub saved trapped x30 at (old_sp - 16). After allocating the frame,
  // that location is at (sp + EXC_FRAME_SIZE).
  ldr x8, [sp, #EXC_FRAME_SIZE]
  str x8, [sp, #EXC_OFF_X30]

  // Call C: x0 = &frame, x1=type, x2=origin
  mov x0, sp
  bl  exception_dispatch

  // Check action
  cmp x0, #1               // EXC_ACTION_RESUME
  b.eq .Lresume

  // EXC_ACTION_KILL is not implemented yet (no task model).
  // Treat it like panic for now.
  b   .Lpanic

.Lresume:
  // Resume path: write back (possibly modified) ELR/SPSR
  ldr x4, [sp, #EXC_OFF_ELR_EL1]
  ldr x5, [sp, #EXC_OFF_SPSR_EL1]
  msr elr_el1,  x4
  msr spsr_el1, x5
  isb

  // Restore GPRs and ERET
  ldp x0,  x1,  [sp, #(0*16)]
  ldp x2,  x3,  [sp, #(1*16)]
  ldp x4,  x5,  [sp, #(2*16)]
  ldp x6,  x7,  [sp, #(3*16)]
  ldp x8,  x9,  [sp, #(4*16)]
  ldp x10, x11, [sp, #(5*16)]
  ldp x12, x13, [sp, #(6*16)]
  ldp x14, x15, [sp, #(7*16)]
  ldp x16, x17, [sp, #(8*16)]
  ldp x18, x19, [sp, #(9*16)]
  ldp x20, x21, [sp, #(10*16)]
  ldp x22, x23, [sp, #(11*16)]
  ldp x24, x25, [sp, #(12*16)]
  ldp x26, x27, [sp, #(13*16)]
  ldp x28, x29, [sp, #(14*16)]
  ldr x30,       [sp, #(15*16)]

  // Pop frame + stub scratch
  add sp, sp, #(EXC_FRAME_SIZE + EXC_STUB_SCRATCH)
  eret

.Lpanic:
  // Panic policy: never return
  // Pop frame + scratch to keep SP sane for debuggers, then low-power spin.
  add sp, sp, #(EXC_FRAME_SIZE + EXC_STUB_SCRATCH)
1: wfe
   b 1b
