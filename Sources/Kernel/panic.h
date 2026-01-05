#pragma once

#include <stdint.h>

/*
 * Always-available kernel panic facility.
 *
 * Bring-up constraints:
 *  - no dynamic allocation
 *  - UART-only output
 *  - parks the CPU
 */

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((noreturn)) void panic(const char *msg);
__attribute__((noreturn)) void panic_with_prefix(const char *prefix, const char *msg);

#ifdef __cplusplus
}
#endif
