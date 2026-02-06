#pragma once

#include <stdint.h>

typedef enum sched_intent {
    SCHED_INTENT_INTERACTIVE = 0,
    SCHED_INTENT_LATENCY = 1,
    SCHED_INTENT_THROUGHPUT = 2,
    SCHED_INTENT_BACKGROUND = 3,
    SCHED_INTENT_COUNT = 4,
} sched_intent_t;

static inline sched_intent_t sched_intent_sanitize(uint32_t raw)
{
    if (raw >= (uint32_t)SCHED_INTENT_COUNT) {
        return SCHED_INTENT_BACKGROUND;
    }
    return (sched_intent_t)raw;
}

