// Canonical Core entrypoints callable by the Kernel (Option A boundary)
//
// Keep signatures stable and POD-only.

#ifndef CAPAZ_CORE_ENTRYPOINTS_H
#define CAPAZ_CORE_ENTRYPOINTS_H

#include <stdint.h>

#include "kernel_services_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

// Core must be provided with the kernel services table before it executes.
//
// Contract:
// - Kernel calls this once during early boot (thread-context).
// - After this, Core may call back into the Kernel via the services table.
void core_set_services(const kernel_services_v1_t *services);

// Primary Core entrypoint. Kernel calls this when it is ready to transfer
// control to Core.
//
// Return value is reserved for future use; Core may not return.
int32_t core_main(void);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CAPAZ_CORE_ENTRYPOINTS_H
