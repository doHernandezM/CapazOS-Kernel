#ifndef GICV2_H
#define GICV2_H

#include <stdint.h>
#include <stdbool.h>

void gicv2_init(void);

/* Enable/disable an interrupt ID (PPI/SPI). */
void gicv2_enable_irq(uint32_t irq);
void gicv2_disable_irq(uint32_t irq);

/* CPU interface acknowledge / EOI. */
uint32_t gicv2_acknowledge(void);
void gicv2_end_interrupt(uint32_t iar);

/* Configure interrupt trigger type (edge=true, level=false). */
void gicv2_config_irq(uint32_t irq, bool edge);

#endif /* GICV2_H */
