#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal platform IRQ HAL.

void hal_irq_init(void);

void hal_irq_enable(uint32_t irq);
void hal_irq_disable(uint32_t irq);
void hal_irq_config(uint32_t irq, bool edge);

uint32_t hal_irq_acknowledge(void);
void hal_irq_end(uint32_t iar);
