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

/*
 * On systems with a split virtual address space the kernel maps all
 * physical addresses into a higher-half direct map.  Once TTBR0 is
 * cleared and only TTBR1 is active, low virtual addresses will
 * generate faults.  To avoid hard‑coding low virtual MMIO addresses
 * in C code, compute the kernel virtual alias from the physical
 * address using the constant HH_PHYS_BASE defined here.  The boot
 * stage maps 0x0000_0000.. and 0x4000_0000.. regions into
 * 0xFFFF800000000000.., so MMIO like the PL011 UART at physical
 * 0x0900_0000 appears at HH_PHYS_BASE + 0x0900_0000.
 */
#define HH_PHYS_BASE 0xFFFF800000000000ULL
#define UART0_BASE ((volatile uint32_t *)(HH_PHYS_BASE + 0x09000000ULL))
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

void uart_putchar(char c)
{
    uart_send((uint8_t)c);
}

void uart_putnl(void)
{
    uart_send((uint8_t)'\r');
    uart_send((uint8_t)'\n');
}

static void uart_put_hex_nibble(uint8_t v)
{
    v &= 0xF;
    uart_send((uint8_t)(v < 10 ? ('0' + v) : ('a' + (v - 10))));
}

void uart_puthex64(uint64_t v)
{
    uart_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        uart_put_hex_nibble((uint8_t)(v >> (uint32_t)shift));
    }
}