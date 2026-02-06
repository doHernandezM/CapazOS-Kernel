#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Minimal platform block HAL.
// Returns false if no platform block device is available.

bool hal_block_init(void);
bool hal_block_ready(void);
uint32_t hal_block_sector_size(void);
uint64_t hal_block_capacity_sectors(void);
bool hal_block_read(uint64_t lba, void *buf, size_t blocks);
bool hal_block_write(uint64_t lba, const void *buf, size_t blocks);
