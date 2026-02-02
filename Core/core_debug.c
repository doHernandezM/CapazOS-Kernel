#include <stddef.h>

#include "serial/uart.h"

// Raw UART debug output callable from Swift without KernelLog.
void core_dbg_uart_write(const char *s, size_t len)
{
    uart_write(s, len);
}
