#pragma once
#include <stdint.h>

// Early console (PL011) for QEMU virt.
// Phase 3.1: support switching the MMIO base from physical (pre-MMU) to the
// kernel's TTBR1-mapped MMIO VA after higher-half bring-up.

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* s);

// Switch the MMIO base used for UART registers.
void uart_set_base(uint64_t base);
uint64_t uart_get_base(void);
