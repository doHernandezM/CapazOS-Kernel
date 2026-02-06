#pragma once

#include <stdint.h>

// Platform configuration glue: selects arch + board settings based on build flags.

// --- Arch selection ---------------------------------------------------------
#if defined(CAPAZ_ARCH_AARCH64)
#include "../Arch/aarch64/arch_config.h"
#else
#error "Unsupported arch: define CAPAZ_ARCH_* in the build"
#endif

// --- Board selection --------------------------------------------------------
#if defined(CAPAZ_BOARD_VIRT)
#include "../Board/virt/board_config.h"
#elif defined(CAPAZ_BOARD_RPI3)
#include "../Board/rpi3/board_config.h"
#else
#error "Unsupported board: define CAPAZ_BOARD_* in the build"
#endif

// --- Derived helpers --------------------------------------------------------

// High-half direct-map base for the RAM window.
#define CAPAZ_HH_RAM_BASE (CAPAZ_HH_BASE + CAPAZ_RAM_BASE)

// End of the RAM direct-map window.
#define CAPAZ_RAM_WINDOW_END (CAPAZ_RAM_BASE + CAPAZ_RAM_DIRECTMAP_SIZE)
