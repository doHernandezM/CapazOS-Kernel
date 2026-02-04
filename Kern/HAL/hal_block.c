#include "hal_block.h"

bool hal_block_read(uint64_t lba, void *buf, size_t blocks)
{
    (void)lba;
    (void)buf;
    (void)blocks;
    return false;
}
