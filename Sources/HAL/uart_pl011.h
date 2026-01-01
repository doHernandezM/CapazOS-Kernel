/*
 * uart_pl011.h
 *
 * Minimal interface for the PL011 UART used on QEMU’s virt platform.
 * The kernel stub uses these functions to print diagnostic messages.
 *
 * Note: These routines are not re‑entrant and busy‑wait on the UART’s
 * transmit FIFO.  They intentionally avoid any dynamic allocation or
 * standard library calls so that they can be used early in the boot.
 */

#pragma once

#include <stdint.h>

/* Write a single byte to the UART. */
void uart_send(uint8_t byte);

/* Write a null‑terminated string to the UART. */
void uart_puts(const char *str);

/* Convenience helpers for early bring-up output. */
void uart_putchar(char c);
void uart_putnl(void);
void uart_puthex64(uint64_t v);