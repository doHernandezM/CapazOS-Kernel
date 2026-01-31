#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdbool.h>

#include "boot_info.h"
#include "dtb.h"

/*
 * Derive usable physical memory ranges:
 *   usable = dtb /memory ranges - (DTB reserved ranges) - (implicit reservations)
 *
 * Implicit reservations include:
 *   - boot stage image + boot tables (RAM_BASE .. kernel_phys_base)
 *   - kernel image
 *   - DTB blob itself
 */
bool platform_get_usable_ranges(const boot_info_t *boot_info,
                                dtb_range_t *out, uint32_t *inout_count);

/* Dump memory/reserved/usable maps in a stable, human-readable format. */
void platform_dump_memory_map(const boot_info_t *boot_info);

#endif /* PLATFORM_H */
