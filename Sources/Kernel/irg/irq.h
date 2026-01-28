#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * Trap frame as saved by kernel_vectors.S IRQ entry stub.
 *
 * Layout is preemption-ready: in addition to saving x0..x30, the
 * IRQ stub also snapshots ELR_EL1 and SPSR_EL1 into the frame. This enables a
 * future "return-from-IRQ into a different thread" design by switching SP to a
 * different saved trap frame and restoring ELR/SPSR before ERET.
 *
 * Note: ESR_EL1/FAR_EL1 are also captured for debugging, but on AArch64 they are
 * architecturally meaningful for synchronous exceptions; their values on IRQ
 * entry may be stale.
 */
typedef struct trap_frame {
    /* x0..x30 saved first. */
    uint64_t x[31];
    uint64_t pad;      /* keeps the GPR area at 32*8 bytes */

    /*
     * Additional fields saved by the exception entry stubs.
     *
     * Requirement: the trap-frame contract must be "real":
     * the assembly layout and this C layout must match exactly so that
     * a future scheduler can safely switch threads by switching the IRQ-return SP.
     */
    uint64_t sp_at_fault; /* SP after allocating the frame */
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t esr_el1;
    uint64_t far_el1;
    uint64_t sp_el0;      /* optional (0 until EL0 exists) */
} trap_frame_t;

/* Compile-time layout checks for the AArch64 vector stubs. */
_Static_assert(offsetof(trap_frame_t, x[0]) == 0, "tf: x0 offset");
_Static_assert(offsetof(trap_frame_t, x[30]) == 30 * 8, "tf: x30 offset");
_Static_assert(offsetof(trap_frame_t, pad) == 31 * 8, "tf: pad offset");
_Static_assert(offsetof(trap_frame_t, sp_at_fault) == 32 * 8, "tf: sp offset");
_Static_assert(offsetof(trap_frame_t, elr_el1) == 32 * 8 + 1 * 8, "tf: elr offset");
_Static_assert(offsetof(trap_frame_t, spsr_el1) == 32 * 8 + 2 * 8, "tf: spsr offset");
_Static_assert(offsetof(trap_frame_t, esr_el1) == 32 * 8 + 3 * 8, "tf: esr offset");
_Static_assert(offsetof(trap_frame_t, far_el1) == 32 * 8 + 4 * 8, "tf: far offset");
_Static_assert(offsetof(trap_frame_t, sp_el0) == 32 * 8 + 5 * 8, "tf: sp_el0 offset");
_Static_assert(sizeof(trap_frame_t) == 32 * 8 + 6 * 8, "tf: size");

typedef void (*irq_handler_t)(uint32_t irq, void *ctx, trap_frame_t *tf);

/* Register an IRQ handler. Returns false if irq out of range. */
bool irq_register(uint32_t irq, irq_handler_t handler, void *ctx);

/* Dispatch function called from the assembly IRQ entry stub. */
void irq_dispatch(trap_frame_t *tf);

/* Global IRQ mask/unmask (DAIF.I). */
void irq_global_enable(void);
void irq_global_disable(void);

/*
 * Robust IRQ masking helpers for critical sections.
 *
 * - irq_save() masks IRQs and returns the previous DAIF value.
 * - irq_restore(prev_daif) restores the IRQ mask bit to the previous state.
 *
 * These are safe to use in nested critical sections:
 *
 *   uint64_t flags = irq_save();
 *   ... critical section ...
 *   irq_restore(flags);
 */
uint64_t irq_save(void);
void irq_restore(uint64_t prev_daif);

/* True if IRQs are currently masked (DAIF.I == 1). */
bool irq_irqs_disabled(void);

/* True if currently executing in IRQ context (nesting-aware). */
bool in_irq(void);

/* Optional explicit enter/exit hooks (used by irq_dispatch). */
void irq_enter(void);
void irq_exit(void);
#endif /* IRQ_H */
