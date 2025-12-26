#include "uart_pl011.h"

#define UART_FR_TXFF (1u << 5)

static volatile uint64_t g_uart_base = 0x09000000ull; // physical PL011 base on QEMU virt
static int uart_initialized = 0;

static inline volatile uint32_t* uart_reg(uint32_t off) {
    return (volatile uint32_t*)(uintptr_t)(g_uart_base + (uint64_t)off);
}

void uart_set_base(uint64_t base) {
    g_uart_base = base;
}

uint64_t uart_get_base(void) {
    return g_uart_base;
}

void uart_init(void) {
    // QEMU virt PL011 is typically ready without additional init for basic TX.
    uart_initialized = 1;
}

void uart_putc(char c) {
    if (!uart_initialized) return;

    volatile uint32_t* UARTDR = uart_reg(0x00);
    volatile uint32_t* UARTFR = uart_reg(0x18);

    while ((*UARTFR) & UART_FR_TXFF) { }
    *UARTDR = (uint32_t)c;
}

void uart_puts(const char* s) {
    if (!uart_initialized) return;

    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}
