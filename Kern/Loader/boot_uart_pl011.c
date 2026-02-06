#include "boot_hal.h"
#include "platform_config.h"

static inline volatile uint32_t *boot_uart_mmio(void) {
    return (volatile uint32_t *)(uintptr_t)CAPAZ_UART_FALLBACK_PHYS_BASE;
}

void boot_uart_putc(char c)
{
    volatile uint32_t *uart = boot_uart_mmio();
    while (uart[0x18 / 4] & (1u << 5)) {
        __asm__ volatile("nop");
    }
    uart[0] = (uint32_t)c;
}

void boot_uart_puts(const char *s)
{
    if (!s) return;
    while (*s) {
        if (*s == '\n') boot_uart_putc('\r');
        boot_uart_putc(*s++);
    }
}

uint64_t boot_time_now(void)
{
    return 0;
}
