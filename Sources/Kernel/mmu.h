#ifndef MMU_H
#define MMU_H

#include <stdint.h>
#include "boot_info.h"

/*
 * Install the kernel TTBR1 page tables and disable TTBR0 (EPD0=1).
 * The boot_info is provided for future DTB-driven memory mapping; it
 * may be NULL in early bring-up but should usually be passed through.
 */
void mmu_init(const boot_info_t *boot_info);

#endif
