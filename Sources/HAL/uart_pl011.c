#include "uart_pl011.h"

/*
 * PL011 UART driver.
 *
 * In early bring-up we assume a high-half mapping exists where
 *   VA = 0xFFFF8000_0000_0000 + PA
 * for the first 1GiB of PA space (device/MMIO region).  The boot stage
 * maps this, and mmu_init() preserves it.
 *
 * The base is configurable so we can switch to the DTB-provided UART
 * address once DTB parsing works.
 */

#define HH_PHYS_BASE 0xFFFF800000000000ULL

/* QEMU virt default (fallback) */
#define UART_FALLBACK_PHYS_BASE 0x09000000ULL

/* Registers */
#define UARTDR   0x00
#define UARTFR   0x18
#define UARTFR_TXFF (1 << 5)

static volatile uint32_t *g_uart_base = (volatile uint32_t *)(HH_PHYS_BASE + UART_FALLBACK_PHYS_BASE);

void uart_init(uint64_t uart_phys_base)
{
    if (uart_phys_base != 0) {
        g_uart_base = (volatile uint32_t *)(HH_PHYS_BASE + uart_phys_base);
    }
}

void uart_putc(char c) {
    while (g_uart_base[UARTFR / 4] & UARTFR_TXFF) {
        __asm__ volatile("nop");
    }
    g_uart_base[UARTDR / 4] = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_putnl(void) {
    uart_puts("\n");
}

void uart_puthex64(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[16];
    for (int i = 15; i >= 0; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    uart_puts("0x");
    for (int i = 0; i < 16; i++) uart_putc(buf[i]);
}
