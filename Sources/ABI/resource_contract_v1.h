// resource_contract_v1.h
// Resource contract ABI v1
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPAZ_RESOURCE_CONTRACT_ABI_V1 1u

typedef enum io_priority_v1 : uint32_t {
    IO_PRIORITY_LOW = 0,
    IO_PRIORITY_NORMAL = 1,
    IO_PRIORITY_HIGH = 2,
    IO_PRIORITY_REALTIME = 3,
} io_priority_t;

typedef struct resource_contract_v1 {
    // Optional CPU budget per epoch. 0 means "no budget specified".
    uint64_t cpu_budget_ticks_per_epoch;

    // Optional latency bound / deadline. 0 means "none".
    uint64_t deadline_ticks;

    // Optional allocation budget per epoch. 0 means "no budget specified".
    uint64_t alloc_budget_bytes_per_epoch;

    io_priority_t io_priority;
    uint32_t _reserved; // padding / future fields
} resource_contract_t;

static inline resource_contract_t resource_contract_default(void) {
    resource_contract_t c;
    c.cpu_budget_ticks_per_epoch = 0;
    c.deadline_ticks = 0;
    c.alloc_budget_bytes_per_epoch = 0;
    c.io_priority = IO_PRIORITY_NORMAL;
    c._reserved = 0;
    return c;
}

#ifdef __cplusplus
} // extern "C"
#endif
