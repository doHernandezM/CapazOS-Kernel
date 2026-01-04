#ifndef CAPAZ_DTB_H
#define CAPAZ_DTB_H

#include <stdint.h>
#include <stdbool.h>

/* Validate and cache a DTB blob. fdt must be a kernel VA. */
bool dtb_init(const void *fdt, uint64_t fdt_size);

/* Returns the DTB header totalsize (0 if dtb_init() has not succeeded). */
uint32_t dtb_get_totalsize(void);

/* Print DTB-derived information to UART (memory, reserved, uart). */
void dtb_dump_summary(void);

/* If a PL011 UART is found, returns true and writes its physical base to *out_phys. */
bool dtb_find_pl011_uart(uint64_t *out_phys);

/* Return the first RAM range found in /memory (base,size). Returns false if missing. */
bool dtb_first_memory_range(uint64_t *out_base, uint64_t *out_size);

/*
 * A simple [base,size] range used for DTB-derived layouts.
 * All addresses are physical addresses.
 */
typedef struct dtb_range {
    uint64_t base;
    uint64_t size;
} dtb_range_t;

/*
 * Structured DTB results.
 *
 * The caller supplies an output array and its capacity in *count.
 * On success, *count is updated to the number of ranges written.
 */
bool dtb_get_memory_ranges(dtb_range_t *out, uint32_t *count);
bool dtb_get_reserved_ranges(dtb_range_t *out, uint32_t *count);

#endif /* CAPAZ_DTB_H */
