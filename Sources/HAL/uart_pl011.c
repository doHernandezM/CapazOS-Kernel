/*
 * uart_pl011.c
 *
 * Basic polled UART routines for the ARM PrimeCell PL011 used on
 * QEMU’s virt board.  These functions are suitable for very early
 * kernel output and avoid any dependencies on a C runtime or
 * allocation.  For simplicity the driver only implements the
 * transmit path.
 */

#include "uart_pl011.h"

#define UART0_BASE ((volatile uint32_t *)0x09000000UL)
#define UART0_DR   (UART0_BASE + 0x00 / sizeof(uint32_t))
#define UART0_FR   (UART0_BASE + 0x18 / sizeof(uint32_t))

/* Bit mask for the transmit FIFO full flag in the UARTFR register. */
#define UARTFR_TXFF (1u << 5)

void uart_send(uint8_t byte)
{
    /* Wait until there is space in the transmit FIFO. */
    while (*UART0_FR & UARTFR_TXFF) {
        /* spin */
    }
    *UART0_DR = (uint32_t)byte;
}

void uart_puts(const char *str)
{
    if (!str) {
        return;
    }
    while (*str) {
        uart_send((uint8_t)*str++);
    }
}