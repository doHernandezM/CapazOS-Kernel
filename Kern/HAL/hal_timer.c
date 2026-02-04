#include "hal_timer.h"

#include "timer_generic.h"

uint64_t hal_time_now(void)
{
    return time_now();
}

void hal_timer_init_hz(uint32_t hz)
{
    timer_init_hz(hz);
}

void hal_timer_handle_irq(void)
{
    timer_handle_irq();
}

uint64_t hal_timer_ticks_read(void)
{
    return timer_ticks_read();
}

uint32_t hal_timer_irq(void)
{
    return (uint32_t)TIMER_PPI_IRQ;
}
