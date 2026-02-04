#pragma once

#include <stdbool.h>

// Minimal platform MMU HAL.
//
// The current kernel owns MMU setup directly; this surface is a placeholder
// for future board-specific hooks.

bool hal_mmu_is_supported(void);
