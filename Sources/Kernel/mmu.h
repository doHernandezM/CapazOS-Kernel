/*
 * mmu.h
 *
 * Simple MMU helpers for the Capaz kernel.  During early bring‑up
 * the boot stage creates a coarse page table that maps the entire
 * physical address space into both low and high virtual address
 * ranges.  After the kernel C runtime is up and running we want
 * to install a new set of translation tables that implement the
 * "default‑deny" policy: user space (TTBR0) is empty and kernel
 * space (TTBR1) contains only those mappings necessary for the
 * kernel to execute.  The mmu_init() function performs this
 * transition.  In its initial form it reuses the same one‑gigabyte
 * block mappings for the device and RAM regions but routes all
 * translations through TTBR1 only and disables TTBR0 via TCR.EPD0.
 * Later revisions will add finer grained mappings and W^X
 * enforcement.
 */

#ifndef CAPAZ_MMU_H
#define CAPAZ_MMU_H

#include <stdint.h>

/* Initialise new kernel translation tables and install them.  This
 * function must be called early in kmain() before any dynamic memory
 * is used.  The boot_info pointer is currently unused but will be
 * required when mapping memory beyond the static identity map.
 */
void mmu_init(void *boot_info);

#endif /* CAPAZ_MMU_H */