#ifndef UART_PL011_H
#define UART_PL011_H

#include <stdint.h>
#include <stdbool.h>

/* Set PL011 base physical address. If 0, keep the current base (fallback default). */
void uart_init(uint64_t uart_phys_base);

/* Optional: explicit hardware init (polled, no interrupts required).
   Provide UART reference clock in Hz for accurate baud.
   If clock_hz == 0 or baud == 0, skips divisor programming and only sets 8N1 + FIFO + enable. */
void uart_hw_init(uint32_t clock_hz, uint32_t baud);

/* TX (polled) */
void uart_putc(char c);
void uart_puts(const char *s);
void uart_putnl(void);

/* Formatting helpers */
void uart_puthex64(uint64_t value);
void uart_putu64_dec(uint64_t value);

/* RX (polled) */
bool uart_rx_ready(void);

/* Returns true if a character was read into *out, false if RX FIFO empty. */
bool uart_getc_nonblock(char *out);

/* Blocking read of one character. */
char uart_getc(void);

#endif /* UART_PL011_H */
