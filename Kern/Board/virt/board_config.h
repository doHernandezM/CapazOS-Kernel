#pragma once

#include <stdint.h>

// QEMU virt board wiring / memory map.

// Physical RAM base for virt.
#define CAPAZ_RAM_BASE 0x40000000ULL

// Direct-map window size for RAM (1 GiB).
#define CAPAZ_RAM_DIRECTMAP_SIZE 0x40000000ULL

// PL011 UART MMIO base for virt (fallback when DTB not available).
#define CAPAZ_UART_FALLBACK_PHYS_BASE 0x09000000ULL
