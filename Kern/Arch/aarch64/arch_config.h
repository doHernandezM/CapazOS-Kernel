#pragma once

#include <stdint.h>

// AArch64 architectural configuration.
// Keep this focused on CPU-level assumptions, not board wiring.

// High-half base used for kernel direct mappings.
#define CAPAZ_HH_BASE 0xFFFF800000000000ULL

// Architectural page size assumption (4 KiB).
#define CAPAZ_ARCH_PAGE_SIZE 0x1000ULL
