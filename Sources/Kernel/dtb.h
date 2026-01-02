#ifndef CAPAZ_DTB_H
#define CAPAZ_DTB_H

#include <stdint.h>
#include <stdbool.h>

/* Validate and cache a DTB blob. fdt must be a kernel VA. */
bool dtb_init(const void *fdt, uint64_t fdt_size);

/* Print DTB-derived information to UART (memory, reserved, uart). */
void dtb_dump_summary(void);

/* If a PL011 UART is found, returns true and writes its physical base to *out_phys. */
bool dtb_find_pl011_uart(uint64_t *out_phys);

/* Return the first RAM range found in /memory (base,size). Returns false if missing. */
bool dtb_first_memory_range(uint64_t *out_base, uint64_t *out_size);

#endif /* CAPAZ_DTB_H */
