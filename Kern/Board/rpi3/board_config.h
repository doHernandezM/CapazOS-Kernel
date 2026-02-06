#pragma once

#include <stdint.h>

// QEMU raspi3b board wiring / memory map.
// NOTE: These values are placeholders for bring-up and may be refined.

// Physical RAM base for rPi3.
#define CAPAZ_RAM_BASE 0x00000000ULL

// Direct-map window size for RAM (1 GiB).
#define CAPAZ_RAM_DIRECTMAP_SIZE 0x40000000ULL

// PL011 UART MMIO base for rPi3 (fallback when DTB not available).
#define CAPAZ_UART_FALLBACK_PHYS_BASE 0x3F201000ULL
