#pragma once

/*
 * boot_config.h
 *
 * This header is generated (or overwritten) by Kernel/Scripts/build-kernel.sh.
 * It provides constants that the boot image can use without taking the address
 * of kernel linker symbols.
 */

#ifndef KERNEL_PHYS_BASE
/* Physical placement of the padded kernel image inside Kernel.img */
#define KERNEL_PHYS_BASE 0x40200000ull
#endif

#ifndef KERNEL_PHYS_END
/* One past the last byte of the kernel image in physical memory (after padding) */
#define KERNEL_PHYS_END  0x00000000ull
#endif

/*
 * Offsets are relative to KERNEL_PHYS_BASE (and also to the kernel VA base,
 * since the kernel is linked as a higher-half image).
 */
#ifndef KERNEL_TEXT_OFF
#define KERNEL_TEXT_OFF   0x00000000ull
#endif

#ifndef KERNEL_TEXT_SIZE
/* Size of the RX text region (page-aligned). */
#define KERNEL_TEXT_SIZE  0x00000000ull
#endif

#ifndef KERNEL_RODATA_OFF
#define KERNEL_RODATA_OFF 0x00000000ull
#endif

#ifndef KERNEL_RODATA_SIZE
/* Size of the RO+NX rodata region (page-aligned). */
#define KERNEL_RODATA_SIZE 0x00000000ull
#endif

#ifndef KERNEL_DATA_OFF
#define KERNEL_DATA_OFF   0x00000000ull
#endif

#ifndef KERNEL_DATA_SIZE
/* Size of the RW+NX data+bss region (page-aligned). */
#define KERNEL_DATA_SIZE  0x00000000ull
#endif

#ifndef KERNEL_STACK_TOP_OFF
#define KERNEL_STACK_TOP_OFF 0x00000000ull
#endif

#ifndef KERNEL_STACK_SIZE
/* Total kernel stack reservation (page-aligned). */
#define KERNEL_STACK_SIZE 0x00000000ull
#endif

/* Offset of the kernel entry point (crt0) from the kernel VA base. */
#ifndef KERNEL_ENTRY_OFFSET
#define KERNEL_ENTRY_OFFSET 0x00000000ull
#endif
 
