#include "hal_block.h"

#include "dev/virtio_blk.h"

bool hal_block_read(uint64_t lba, void *buf, size_t blocks)
{
    if (!virtio_blk_ready() || buf == NULL || blocks == 0) {
        return false;
    }

    uint32_t sector_size = virtio_blk_sector_size();
    if (sector_size == 0) {
        return false;
    }

    size_t total_bytes = (size_t)sector_size * blocks;
    return virtio_blk_read(lba, (uint32_t)blocks, buf, total_bytes);
}
