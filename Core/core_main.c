#include <stdint.h>

#include "core_sections.h"
// Core ↔ Kernel internal API.  Core calls kernel mechanisms directly.
#include "api/core_kernel_api.h"

// The old services table no longer exists.  Core calls kernel
// mechanisms directly via the internal API.

// Temporary Core entrypoint.
//
// Kernel side:
//   core_set_services(kernel_services_v1());
//   core_main();
//
// This keeps the ABI boundary POD-only while allowing Core to access services
// through runtime shims.
int32_t core_main(void) {
    // Early log entry via the internal API.
    cka_log_write("[core] core_main entered\n");
    // Call into the Swift Core if available.  The Swift side exports
    // core_main_swift() with a C symbol via @_cdecl.  This call will
    // transfer control into Swift and return its exit code.  If the
    // Swift object was not linked (no Swift sources), this symbol
    // resolves to zero by weak linking semantics and falls through.
    extern int32_t core_main_swift(void);
    return core_main_swift();
}
