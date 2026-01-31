#include "timer_generic.h"

#include "config.h"

/*
 * ARM Generic Timer (AArch64) - CNTV (virtual timer).
 *
 * Clocksource: CNTVCT_EL0
 * Clockevent:  CNTV_{TVAL,CVAL,CTL}_EL0
 */

static volatile uint64_t s_ticks;

typedef enum {
    EVENT_MODE_OFF = 0,
    EVENT_MODE_PERIODIC,
    EVENT_MODE_ONESHOT,
} event_mode_t;

static event_mode_t s_mode = EVENT_MODE_OFF;
static uint64_t s_period_ticks;
static uint64_t s_next_deadline;

static inline uint64_t read_cntfrq(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline uint64_t read_cntvct(void)
{
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

//static inline void write_cntv_tval(uint32_t v)
//{
//    /* CNTV_TVAL_EL0 is 32-bit: force a W-register operand to satisfy clang. */
//    __asm__ volatile("msr cntv_tval_el0, %w0" :: "r"(v));
//}

static inline void write_cntv_cval(uint64_t v)
{
    __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(v));
}

static inline void write_cntv_ctl(uint32_t v)
{
    /* Some assemblers reject %w0 here; use 64-bit operand and mask to 32 bits. */
    uint64_t val = (uint64_t)(v & 0xFFFFFFFFu);
    __asm__ volatile("msr cntv_ctl_el0, %0\n\tisb" :: "r"(val) : "memory");
}

uint64_t time_now(void)
{
    return read_cntvct();
}

static uint64_t hz_to_period_ticks(uint32_t hz)
{
    if (hz == 0) {
        return 0;
    }
    return read_cntfrq() / (uint64_t)hz;
}

void event_arm_periodic(uint32_t hz)
{
    uint64_t period = hz_to_period_ticks(hz);
    if (period == 0) {
        s_mode = EVENT_MODE_OFF;
        write_cntv_ctl(0);
        return;
    }

    s_period_ticks = period;
    s_mode = EVENT_MODE_PERIODIC;

    /* Program the next firing using an absolute compare (CVAL). */
    s_next_deadline = time_now() + s_period_ticks;
    write_cntv_cval(s_next_deadline);

    /* enable=1, imask=0 */
    write_cntv_ctl(0x1);
}

void event_arm_oneshot(uint64_t absolute_deadline)
{
    s_mode = EVENT_MODE_ONESHOT;

    /* Program absolute compare value (CVAL). */
    write_cntv_cval(absolute_deadline);

    /* enable=1, imask=0 */
    write_cntv_ctl(0x1);
}

void event_handle_irq(void)
{
    /* Any timer interrupt implies the compare fired. */
    s_ticks++;

    if (s_mode == EVENT_MODE_PERIODIC) {
        /* Rearm by scheduling the next absolute deadline. */
        uint64_t now = time_now();

        /*
         * If we serviced late (e.g. interrupts masked), avoid drifting into
         * the past.
         */
        if (now >= s_next_deadline) {
            s_next_deadline = now + s_period_ticks;
        } else {
            s_next_deadline += s_period_ticks;
        }

        write_cntv_cval(s_next_deadline);
        return;
    }

    /* One-shot: disarm to avoid refiring on a stale compare. */
    s_mode = EVENT_MODE_OFF;
    write_cntv_ctl(0x0);
}

/* ---------------- Compatibility wrappers ---------------- */

void timer_init_hz(uint32_t hz)
{
#if CONFIG_TICKLESS
    (void)hz;
    /* Tickless build: do not start a periodic tick. */
    s_mode = EVENT_MODE_OFF;
    write_cntv_ctl(0x0);
#else
    event_arm_periodic(hz);
#endif
}

void timer_handle_irq(void)
{
    event_handle_irq();
}

uint64_t timer_ticks_read(void)
{
    return s_ticks;
}

