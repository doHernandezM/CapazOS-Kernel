#ifndef CAPAZ_BOOT_INFO_H
#define CAPAZ_BOOT_INFO_H

#include <stdint.h>

/*
 * Boot handoff structure produced by Sources/Arch/aarch64/start.S.
 *
 * dtb_ptr is a *kernel virtual address* in the high-half direct map
 * (TTBR1) so the kernel can safely disable TTBR0.
 */
typedef struct boot_info {
    uint64_t kernel_phys_base;      /* Physical address where the kernel image starts. */
    uint64_t kernel_loaded_size;    /* Size of the kernel image bytes (through .data). */
    uint64_t kernel_runtime_size;   /* Size of the kernel runtime footprint (through .bss). */
    uint64_t kernel_entry_offset;   /* Entry point offset from kernel_phys_base. */

    uint64_t dtb_ptr;               /* High-half VA of the DTB blob (0 if none). */
    uint64_t dtb_size;              /* Size of DTB blob in bytes (0 if unknown). */
} boot_info_t;

#endif /* CAPAZ_BOOT_INFO_H */
