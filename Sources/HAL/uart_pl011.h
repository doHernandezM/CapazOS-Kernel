#ifndef UART_PL011_H
#define UART_PL011_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Set PL011 base physical address. If 0, keep the current base (fallback default). */
void uart_init(uint64_t uart_phys_base);

/* Optional: explicit hardware init (polled, no interrupts required).
   Provide UART reference clock in Hz for accurate baud.
   If clock_hz == 0 or baud == 0, skips divisor programming and only sets 8N1 + FIFO + enable. */
void uart_hw_init(uint32_t clock_hz, uint32_t baud);

/* IRQ support (RX only for Phase 1).
 * These APIs are safe to call after uart_hw_init(). */
void uart_enable_rx_irq(void);
void uart_disable_rx_irq(void);

/* Drain RX FIFO into out_buf (up to max bytes). Callable from an IRQ handler.
 * Returns true if any bytes were drained.
 * - out_n is set to the number of bytes drained (0 if none)
 * - No allocation, no blocking
 */
bool uart_irq_drain_rx(char *out_buf, size_t max, size_t *out_n);

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
