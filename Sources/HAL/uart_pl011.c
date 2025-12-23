//
//  uart_pl011.c
//  OSpost
//
//  Created by Cosas on 12/20/25.
//

#include "uart_pl011.h"

#define UART0_BASE 0x09000000UL

#define UARTDR   (*(volatile uint32_t*)(UART0_BASE + 0x00))
#define UARTFR   (*(volatile uint32_t*)(UART0_BASE + 0x18))
#define FR_TXFF  (1u << 5)

/* Initialization guard */
static int uart_initialized = 0;

void uart_init(void) {
    // QEMU virt PL011 is typically ready without init for basic TX.
    uart_initialized = 1;
}

void uart_putc(char c) {
    if (!uart_initialized) {
        return;
    }

    while (UARTFR & FR_TXFF) { }
    UARTDR = (uint32_t)c;
}

void uart_puts(const char* s) {
    if (!uart_initialized) {
        return;
    }

    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
