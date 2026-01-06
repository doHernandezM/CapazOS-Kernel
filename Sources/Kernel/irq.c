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

    /* Defensive: ignore out-of-range IDs while still EOIR'ing. */
    if (id >= IRQ_MAX) {
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
    __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
}

void irq_global_disable(void)
{
    /* daifset #2 sets the IRQ mask bit (I). */
    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
}

uint64_t irq_save(void)
{
    /*
     * Read current DAIF and mask IRQs.
     *
     * The ISB ensures subsequent instructions execute with the updated PSTATE.
     */
    uint64_t daif;
    __asm__ volatile(
        "mrs %0, daif\n\t"
        "msr daifset, #2\n\t"
        "isb\n\t"
        : "=r"(daif)
        :
        : "memory");
    return daif;
}

void irq_restore(uint64_t prev_daif)
{
    /*
     * Only restore the IRQ mask bit based on the saved DAIF.I value.
     *
     * This makes nested usage safe:
     *  - If IRQs were already masked, keep them masked.
     *  - If IRQs were unmasked, unmask them.
     */
    const uint64_t DAIF_I_BIT = (1ull << 7);
    if (prev_daif & DAIF_I_BIT) {
        __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
    } else {
        __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
    }
}

bool irq_irqs_disabled(void)
{
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif) :: "memory");
    return (daif & (1ull << 7)) != 0;
}
