#include "timer_generic.h"

/* Allocation-free, minimal generic timer. */
static volatile uint64_t s_ticks = 0;
static uint64_t s_interval = 0;

static inline uint64_t read_cntfrq(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

/* Use EL1 virtual timer (CNTV_*). */
static inline void write_cntv_tval(uint64_t v)
{
    __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(v));
}

static inline void write_cntv_ctl(uint64_t v)
{
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(v));
}

void timer_init_hz(uint32_t hz)
{
    uint64_t freq = read_cntfrq();
    if (hz == 0) hz = 1;
    s_interval = freq / (uint64_t)hz;
    if (s_interval == 0) s_interval = 1;

    /* Program initial interval and enable the EL1 virtual timer. */
    write_cntv_tval(s_interval);
    write_cntv_ctl(1u); /* ENABLE=1, IMASK=0 */
}

void timer_handle_irq(void)
{
    /* Re-arm the timer for the next tick, then bump counter. */
    write_cntv_tval(s_interval);
    s_ticks++;
}

uint64_t timer_ticks_read(void)
{
    return s_ticks;
}
