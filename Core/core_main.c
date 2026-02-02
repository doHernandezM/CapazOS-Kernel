#include <stdint.h>

// The build scripts compile Core with include paths rooted at the Kernel
// directory (e.g. -I .../Kern/Kernel). Keep includes relative to that.
#include "api/core_boot_if.h"
#include "api/core_kernel_api.h"

// A3: keep a stable C boot hook and immediately transfer control to Swift.
// If Swift is not available yet, keep the system alive and log.
extern __attribute__((weak)) int32_t core_main_swift(void);

static const core_boot_if_t *g_core_boot;

void core_boot_attach(const core_boot_if_t *boot_if)
{
    g_core_boot = boot_if;
}

const core_boot_if_t *core_boot_if(void)
{
    return g_core_boot;
}

int32_t core_main(void)
{
    if (core_main_swift) {
        return core_main_swift();
    }

    cka_early_log("[core] Swift core_main_swift missing; staying in C\n");
    for (;;) {
        cka_yield();
    }
}
