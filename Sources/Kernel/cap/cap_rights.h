#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t cap_rights_t;

// Common rights
#define CAP_R_READ     ((cap_rights_t)1u << 0)
#define CAP_R_WRITE    ((cap_rights_t)1u << 1)
#define CAP_R_EXEC     ((cap_rights_t)1u << 2)

#define CAP_R_DUP      ((cap_rights_t)1u << 3)   // may duplicate/derive
#define CAP_R_TRANSFER ((cap_rights_t)1u << 4)   // may transfer ownership

// Thread/Task
#define CAP_R_CONTROL  ((cap_rights_t)1u << 8)

// Endpoint
#define CAP_R_SEND     ((cap_rights_t)1u << 12)
#define CAP_R_RECV     ((cap_rights_t)1u << 13)

// Timer/IRQ tokens
#define CAP_R_ARM      ((cap_rights_t)1u << 16)
#define CAP_R_ACK      ((cap_rights_t)1u << 17)

static inline bool cap_rights_has(cap_rights_t rights, cap_rights_t needed) {
    return (rights & needed) == needed;
}
