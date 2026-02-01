// core_boot_if.h
//
// Kernel -> Core bootstrap interface.
//
// This is intentionally tiny: Kernel owns mechanisms; Core owns policy/services.
// The kernel may only call into Core through this interface during bring-up.

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Called once during kernel bring-up.
// Returns 0 on success; non-zero for an unrecoverable failure.
int32_t core_main(void);

#ifdef __cplusplus
} // extern "C"
#endif
