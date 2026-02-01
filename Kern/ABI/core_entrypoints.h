//
// Core entrypoints and ABI handoff between Kernel and Core.
//
// Design goals:
//  - Core is treated as a required component of the system build.
//  - Kernel seeds newer service ABIs (v3) while Core can still consume v1.
//

#ifndef CORE_ENTRYPOINTS_H
#define CORE_ENTRYPOINTS_H

#include <stdint.h>

#include "core_kernel_abi.h"
#include "core_kernel_abi_v3.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called by the Kernel once basic services are ready.
int core_main(void);

// ---- Services ABI (v1) ----
// Historically exposed as "core_set_services" / "core_services_v1".
void core_set_services(const kernel_services_v1_t *services);
const kernel_services_v1_t *core_services_v1(void);

// Back-compat aliases (older code used _v1 suffix on the setter/getter).
static inline void core_set_services_v1(const kernel_services_v1_t *services) {
  core_set_services(services);
}
static inline const kernel_services_v1_t *core_services(void) {
  return core_services_v1();
}

// ---- Services ABI (v3) ----
// Kernel can seed v3 (capability aware) services; Core may still use v1
// logging early. Core's implementation should backfill v1 from v3.
void core_set_services_v3(const kernel_services_v3_t *services);
const kernel_services_v3_t *core_services_v3(void);

#ifdef __cplusplus
}
#endif

#endif // CORE_ENTRYPOINTS_H
