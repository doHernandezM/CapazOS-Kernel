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

__attribute__((weak)) void core_set_services(const kernel_services_v1_t *services) {
    (void)services;
}

__attribute__((weak)) int32_t core_main(void) {
    return -1;
}
