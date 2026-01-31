#include <stdint.h>

#include "core_sections.h"
#include "core_kernel_abi.h"

// Provided by swift_runtime_shims.c. Returns the last services table provided by
// the Kernel via core_set_services().
extern const kernel_services_v1_t *core_services_v1(void);

// Temporary Core entrypoint.
//
// Kernel side:
//   core_set_services(kernel_services_v1());
//   core_main();
//
// This keeps the ABI boundary POD-only while allowing Core to access services
// through runtime shims.
int32_t core_main(void) {
    const kernel_services_v1_t *services = core_services_v1();
    if (services && services->log) {
        services->log("[core] core_main entered\n");
    }
    return 0;
}
