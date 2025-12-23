// exceptions.h
// OSpost
//
// Exception ABI contract between vectors.S and C.
// Phase 1: add policy outcomes + keep ABI checks.

#ifndef EXCEPTIONS_H
#define EXCEPTIONS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- ABI constants (must match vectors.S) ----
enum {
    EXC_N_GPRS        = 31,     // x0..x30
    EXC_FRAME_SIZE    = 288,    // 16-byte aligned

    EXC_OFF_X0        = 0,
    EXC_OFF_X30       = 30 * 8, // 240

    EXC_OFF_ESR_EL1   = 31 * 8, // 248
    EXC_OFF_ELR_EL1   = 32 * 8, // 256
    EXC_OFF_SPSR_EL1  = 33 * 8, // 264
    EXC_OFF_FAR_EL1   = 34 * 8, // 272
    EXC_OFF_RESERVED  = 35 * 8  // 280 (padding)
};

// Exception type and origin (derived by vectors.S from the vector slot)
typedef enum {
    EXC_TYPE_SYNC   = 0,
    EXC_TYPE_IRQ    = 1,
    EXC_TYPE_FIQ    = 2,
    EXC_TYPE_SERROR = 3,
} exc_type_t;

typedef enum {
    EXC_ORIGIN_CUR_SP0 = 0,
    EXC_ORIGIN_CUR_SPX = 1,
    EXC_ORIGIN_LOW_A64 = 2,
    EXC_ORIGIN_LOW_A32 = 3,
} exc_origin_t;

// Handler return policy
typedef enum {
    EXC_ACTION_PANIC  = 0,   // fatal for now
    EXC_ACTION_RESUME = 1,   // restore ELR/SPSR and ERET
    EXC_ACTION_KILL   = 2,   // Phase 1: defined; Phase 2+: kill current task
} exc_action_t;

// Frame layout must match vectors.S save/restore.
typedef struct exception_frame {
    uint64_t x[EXC_N_GPRS];  // x0..x30 (x30 = LR)
    uint64_t esr_el1;
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t far_el1;
    uint64_t reserved;       // padding (keeps size 288 bytes)
} exception_frame_t;

// Compile-time ABI checks (catches silent drift).
_Static_assert(sizeof(exception_frame_t) == EXC_FRAME_SIZE, "exception_frame_t size mismatch");
_Static_assert(offsetof(exception_frame_t, x)        == EXC_OFF_X0,       "x[] offset mismatch");
_Static_assert(offsetof(exception_frame_t, esr_el1)  == EXC_OFF_ESR_EL1,  "esr_el1 offset mismatch");
_Static_assert(offsetof(exception_frame_t, elr_el1)  == EXC_OFF_ELR_EL1,  "elr_el1 offset mismatch");
_Static_assert(offsetof(exception_frame_t, spsr_el1) == EXC_OFF_SPSR_EL1, "spsr_el1 offset mismatch");
_Static_assert(offsetof(exception_frame_t, far_el1)  == EXC_OFF_FAR_EL1,  "far_el1 offset mismatch");

// C handler returns action for vectors.S (resume vs panic vs kill).
exc_action_t exception_dispatch(exception_frame_t* f, uint64_t type, uint64_t origin);

#ifdef __cplusplus
}
#endif

#endif // EXCEPTIONS_H
