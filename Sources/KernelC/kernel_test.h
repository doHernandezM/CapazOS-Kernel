//
//  kernel_test.h
//  OSpost
//
//  Optional bring-up tests. Keep disabled by default.
//

#pragma once
#include <stdint.h>

#ifndef KTEST_ENABLE
#define KTEST_ENABLE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Stages (used by crt0.c and kernel_test.c)
typedef enum {
  KTEST_STAGE_PRE_MMU    = 0,
  KTEST_STAGE_POST_MMU   = 1,
  KTEST_STAGE_POST_CACHE = 2,
  KTEST_STAGE_KMAIN      = 3,
} ktest_stage_t;

// Feature flags (kernel_test.c uses these masks)
enum {
  KTEST_F_MMU        = (1u << 0),
  KTEST_F_CACHE      = (1u << 1),
  KTEST_F_EXCEPTIONS = (1u << 2),
  KTEST_F_WX         = (1u << 3),
  KTEST_F_NX         = (1u << 4),
  KTEST_F_GUARD      = (1u << 5),
  KTEST_F_IRQ        = (1u << 6),
};

// Default enabled test set (safe baseline)
#ifndef KTEST_DEFAULT_FLAGS
#define KTEST_DEFAULT_FLAGS (KTEST_F_MMU | KTEST_F_CACHE | KTEST_F_EXCEPTIONS)
#endif

// Stage runner (used by crt0.c)
void ktest_run_stage(ktest_stage_t stage);

// Optional runtime control
void ktest_set_flags(uint32_t flags);

// Original “one-shot” test runner (used by kmain.c)
void kernel_test_run(void);

// Optional observation hook (exceptions.c may call this if you wired it)
void ktest_exception_observed(uint64_t type, uint64_t origin, uint32_t ec, uint32_t iss);

#ifdef __cplusplus
}
#endif
