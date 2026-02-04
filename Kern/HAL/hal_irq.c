#include "hal_irq.h"

#include "gicv2.h"

void hal_irq_init(void)
{
    gicv2_init();
}

void hal_irq_enable(uint32_t irq)
{
    gicv2_enable_irq(irq);
}

void hal_irq_disable(uint32_t irq)
{
    gicv2_disable_irq(irq);
}

void hal_irq_config(uint32_t irq, bool edge)
{
    gicv2_config_irq(irq, edge);
}

uint32_t hal_irq_acknowledge(void)
{
    return gicv2_acknowledge();
}

void hal_irq_end(uint32_t iar)
{
    gicv2_end_interrupt(iar);
}
