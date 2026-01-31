// Canonical boundary umbrella header (Option A boundary)
//
// This header exists as the stable include used by both Kernel and Core.
// Keep it small and versioned; include only other ABI headers.

#pragma once

// Core<->Kernel ABI version: bump on any breaking change to *any* boundary header.
#define CAPAZ_CORE_KERNEL_ABI_VERSION 1

#include "kernel_services_v1.h"
#include "core_entrypoints.h"

// Backwards-compatibility: some code may still refer to this macro name.
#ifndef KERNEL_SERVICES_ABI_VERSION
#define KERNEL_SERVICES_ABI_VERSION CAPAZ_KERNEL_SERVICES_V1_MAJOR
#endif

// Backwards-compatibility: keep the older umbrella ABI macro.
#ifndef KERNEL_ABI_VERSION
#define KERNEL_ABI_VERSION CAPAZ_CORE_KERNEL_ABI_VERSION
#endif
