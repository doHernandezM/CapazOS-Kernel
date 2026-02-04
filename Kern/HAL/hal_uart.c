#include "hal_uart.h"

#include "uart_pl011.h"

void hal_uart_init(uint64_t uart_phys_base)
{
    uart_init(uart_phys_base);
}

void hal_uart_hw_init(uint32_t clock_hz, uint32_t baud)
{
    uart_hw_init(clock_hz, baud);
}

void hal_uart_putc(char c)
{
    uart_putc(c);
}

void hal_uart_puts(const char *s)
{
    uart_puts(s);
}

void hal_uart_putnl(void)
{
    uart_putnl();
}

void hal_uart_puthex64(uint64_t value)
{
    uart_puthex64(value);
}

void hal_uart_putu64_dec(uint64_t value)
{
    uart_putu64_dec(value);
}

bool hal_uart_rx_ready(void)
{
    return uart_rx_ready();
}

bool hal_uart_getc_nonblock(char *out)
{
    return uart_getc_nonblock(out);
}

char hal_uart_getc(void)
{
    return uart_getc();
}
