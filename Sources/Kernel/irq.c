#include "irq.h"

#include "gicv2.h"

enum { IRQ_MAX = 1024 };

static irq_handler_t s_handlers[IRQ_MAX];
static void *s_ctx[IRQ_MAX];

bool irq_register(uint32_t irq, irq_handler_t handler, void *ctx)
{
    if (irq >= IRQ_MAX) return false;
    s_handlers[irq] = handler;
    s_ctx[irq] = ctx;
    return true;
}

static inline uint32_t gic_irqid(uint32_t iar)
{
    return (iar & 0x3FFu);
}

void irq_dispatch(trap_frame_t *tf)
{
    /* Acknowledge the interrupt at the GIC CPU interface. */
    uint32_t iar = gicv2_acknowledge();
    uint32_t id = gic_irqid(iar);

    /* 1020-1023 are special/spurious IDs in GICv2. */
    if (id >= 1020u) {
        /*
         * Still write EOIR for the IAR value.
         * On some QEMU/GIC setups, returning without EOIR here can
         * produce an IRQ storm (the CPU keeps taking the same IRQ entry).
         */
        gicv2_end_interrupt(iar);
        return;
    }

    irq_handler_t h = s_handlers[id];
    if (h) {
        h(id, s_ctx[id], tf);
    }

    /* End-of-interrupt (write back the original IAR value). */
    gicv2_end_interrupt(iar);
}

void irq_global_enable(void)
{
    /* daifclr #2 clears the IRQ mask bit (I). */
    __asm__ volatile("msr daifclr, #2" ::: "memory");
}

void irq_global_disable(void)
{
    /* daifset #2 sets the IRQ mask bit (I). */
    __asm__ volatile("msr daifset, #2" ::: "memory");
}
