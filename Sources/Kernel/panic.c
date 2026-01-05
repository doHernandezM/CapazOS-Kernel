#include "panic.h"

#include "uart_pl011.h"

static inline __attribute__((noreturn)) void park_cpu(void)
{
    for (;;) {
        __asm__ volatile("wfe");
    }
}

__attribute__((noreturn))
void panic_with_prefix(const char *prefix, const char *msg)
{
    /* Best-effort: ensure UART is on a valid base even in very early paths. */
    uart_init(0);

    if (prefix && *prefix) {
        uart_puts(prefix);
    } else {
        uart_puts("PANIC: ");
    }

    if (msg && *msg) {
        uart_puts(msg);
    } else {
        uart_puts("<no message>");
    }

    uart_puts("\n");

    park_cpu();
}

__attribute__((noreturn))
void panic(const char *msg)
{
    panic_with_prefix("PANIC: ", msg);
}
