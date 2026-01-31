#ifndef TIMER_GENERIC_H
#define TIMER_GENERIC_H

#include <stdint.h>

/*
 * Clocksource + clockevent split.
 *
 * The underlying implementation uses the ARM Generic Timer (CNTV).
 * - time_now() is the clocksource: read the current counter.
 * - event_*() is the clockevent: program the compare and handle IRQs.
 */

/* Clocksource: current counter value (CNTVCT) in counter ticks. */
uint64_t time_now(void);

/* Clockevent: arm oneshot at an absolute counter deadline (CNTVCT units). */
void event_arm_oneshot(uint64_t deadline);

/* Clockevent: arm periodic interrupts at the given rate (Hz). */
void event_arm_periodic(uint32_t hz);

/* Clockevent: disable event generation. */
void event_disable(void);

/* Clockevent: IRQ handler bookkeeping + re-arm as needed. */
void event_handle_irq(void);

/* Compatibility wrappers used by existing kernel code. */
void timer_init_hz(uint32_t hz);
void timer_handle_irq(void);
uint64_t timer_ticks_read(void);

/*
 * QEMU virt (and many EL2-present environments): the EL1 virtual timer
 * is the most reliable architected timer source. It is delivered as PPI 27.
 */
enum { TIMER_PPI_IRQ = 27 };

#endif /* TIMER_GENERIC_H */
