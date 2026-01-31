// Weak stub implementations for Core entrypoints.
//
// Rationale:
// - The Kernel target should build and link even when the Core object files are not
//   part of the link (early bring-up, unit testing, etc.).
// - When Core is linked, its strong definitions override these stubs.
//
// This avoids relying on toolchain-specific weak-import NULL checks for functions.

#include <stdint.h>

#include "core_entrypoints.h"

// Only used for a loud runtime diagnostic when Core is not linked in.
// The Kernel already ships this HAL for early boot logging.
#include "uart_pl011.h"

__attribute__((weak)) void core_set_services(const kernel_services_v1_t *services) {
    (void)services;
}

__attribute__((weak)) void core_set_services_v3(const kernel_services_v3_t *services) {
    (void)services;
}

__attribute__((weak)) const kernel_services_v3_t *core_services_v3(void) {
    return (const kernel_services_v3_t *)0;
}

__attribute__((weak)) int32_t core_main(void) {
    // If you see this line, the Kernel was built/run without linking in Core.
    // This explains "no Core output" runs and prevents silent confusion.
    uart_puts("[core] stub core_main called; Core not linked\n");
    return -1;
}
