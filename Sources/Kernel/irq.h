#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Trap frame as saved by kernel_vectors.S IRQ/sync entry stubs.
 * Layout matches: 32*8 bytes total (x0..x30 + 8 bytes padding) and is 16B aligned.
 */
typedef struct trap_frame {
    uint64_t x[31];   /* x0..x30 */
    uint64_t pad;     /* unused, keeps frame size = 32*8 */
} trap_frame_t;

typedef void (*irq_handler_t)(uint32_t irq, void *ctx, trap_frame_t *tf);

/* Register an IRQ handler. Returns false if irq out of range. */
bool irq_register(uint32_t irq, irq_handler_t handler, void *ctx);

/* Dispatch function called from the assembly IRQ entry stub. */
void irq_dispatch(trap_frame_t *tf);

/* Global IRQ mask/unmask (DAIF.I). */
void irq_global_enable(void);
void irq_global_disable(void);

#endif /* IRQ_H */
