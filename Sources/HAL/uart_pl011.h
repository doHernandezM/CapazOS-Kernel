#ifndef UART_PL011_H
#define UART_PL011_H

#include <stdint.h>

/* Set PL011 base physical address. If 0, keep the current base (fallback default). */
void uart_init(uint64_t uart_phys_base);

void uart_putc(char c);
void uart_puts(const char *s);
void uart_putnl(void);
void uart_puthex64(uint64_t value);

#endif
