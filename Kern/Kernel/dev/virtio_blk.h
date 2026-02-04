#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool virtio_blk_init(void);
bool virtio_blk_ready(void);
uint32_t virtio_blk_sector_size(void);
uint64_t virtio_blk_capacity_sectors(void);

bool virtio_blk_read(uint64_t lba, uint32_t count, void *buf, size_t buf_len);
bool virtio_blk_write(uint64_t lba, uint32_t count, const void *buf, size_t buf_len);
