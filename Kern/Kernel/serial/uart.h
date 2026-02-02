#pragma once

#include <stddef.h>

// Compatibility wrapper: some higher-level code expects a generic UART header
// at "serial/uart.h". The current kernel uses the PL011 implementation.
//
// This header intentionally re-exports the UART debugging/console functions
// (e.g. uart_puts, uart_puthex64, etc.) used throughout the kernel.

#include "../platform/uart_pl011.h"

static inline void uart_write(const char *s, size_t len)
{
    if (!s) return;
    for (size_t i = 0; i < len; i++) {
        uart_putc(s[i]);
    }
}
