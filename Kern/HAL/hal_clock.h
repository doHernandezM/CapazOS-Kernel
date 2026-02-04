#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal platform clock HAL.

bool hal_clock_set_rate(uint32_t clock_id, uint32_t hz);
