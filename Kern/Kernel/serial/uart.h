#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Compatibility wrapper: some higher-level code expects a generic UART header
// at "serial/uart.h". The current kernel uses the PL011 implementation.
//
// This header intentionally re-exports the UART debugging/console functions
// (e.g. uart_puts, uart_puthex64, etc.) used throughout the kernel.

#include "hal_uart.h"

static inline void uart_putc(char c) { hal_uart_putc(c); }
static inline void uart_puts(const char *s) { hal_uart_puts(s); }
static inline void uart_putnl(void) { hal_uart_putnl(); }
static inline void uart_puthex64(uint64_t value) { hal_uart_puthex64(value); }
static inline void uart_putu64_dec(uint64_t value) { hal_uart_putu64_dec(value); }
static inline bool uart_rx_ready(void) { return hal_uart_rx_ready(); }
static inline bool uart_getc_nonblock(char *out) { return hal_uart_getc_nonblock(out); }
static inline char uart_getc(void) { return hal_uart_getc(); }

static inline void uart_write(const char *s, size_t len)
{
    if (!s) return;
    for (size_t i = 0; i < len; i++) {
        hal_uart_putc(s[i]);
    }
}
