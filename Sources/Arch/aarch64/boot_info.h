//
//  boot_info.h
//  Capaz
//
//  Created by Cosas on 12/26/25.
// 
/*
 * boot_info.h
 *
 * Defines the structure passed from the Capaz boot stage to the
 * higher‑half kernel. When the boot image finishes bringing up the
 * processor and MMU it populates an instance of this structure and
 * passes its address in x0 to the kernel entry point. The kernel
 * may use the information here to discover its physical load address,
 * virtual address mapping and any additional handoff state.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct boot_info {
    /* Physical base address at which the kernel image has been loaded. */
    uint64_t kernel_phys_base;

    /* Virtual base address at which the kernel image is mapped. */
    uint64_t kernel_va_base;

    /* Virtual address of the kernel entry point (crt0). The boot
     * code branches to this address after completing setup. */
    uint64_t kernel_entry_va;

    /* Reserved for future use. Set to zero for now. */
    uint64_t reserved[5];
} boot_info_t;

#ifdef __cplusplus
}
#endif
