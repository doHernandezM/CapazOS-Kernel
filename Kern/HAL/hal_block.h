#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal platform block HAL.
// Returns false if no platform block device is available.

bool hal_block_read(uint64_t lba, void *buf, size_t blocks);
