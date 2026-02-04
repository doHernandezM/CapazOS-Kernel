#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal platform UART HAL.
//
// This surface is intentionally small and is expected to be backed by a
// board-specific implementation (e.g. PL011 for QEMU virt and rPi).

void hal_uart_init(uint64_t uart_phys_base);
void hal_uart_hw_init(uint32_t clock_hz, uint32_t baud);

void hal_uart_putc(char c);
void hal_uart_puts(const char *s);
void hal_uart_putnl(void);

void hal_uart_puthex64(uint64_t value);
void hal_uart_putu64_dec(uint64_t value);

bool hal_uart_rx_ready(void);
bool hal_uart_getc_nonblock(char *out);
char hal_uart_getc(void);
