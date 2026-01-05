#ifndef TIMER_GENERIC_H
#define TIMER_GENERIC_H

#include <stdint.h>

void timer_init_hz(uint32_t hz);
void timer_handle_irq(void);
uint64_t timer_ticks_read(void);

/*
 * QEMU virt (and many EL2-present environments): the EL1 virtual timer
 * is the most reliable architected timer source. It is delivered as PPI 27.
 */
enum { TIMER_PPI_IRQ = 27 };

#endif /* TIMER_GENERIC_H */
