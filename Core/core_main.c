// core_main.c
//
// Stable C boot hook called by the kernel once Core is ready to run.
//
// After the Kernel+Core merge, this is intentionally a thin trampoline into
// Swift policy/services. The Swift entrypoint is optional during bring-up: if
// it is not linked yet, Core will simply return and the kernel will continue
// running its background work loop.

#include <stdint.h>

#include "core_sections.h"
#include "api/core_kernel_api.h"

// Swift entrypoint. When Swift is linked, it should provide a strong symbol.
extern int32_t core_main_swift(void) __attribute__((weak));


int32_t core_main(void)
{
    if (core_main_swift) {
        return core_main_swift();
    }

    cka_log_write("[core] core_main_swift not linked; Core is idle\n");
    return 0;
}
