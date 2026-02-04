#include "panic.h"

#include "hal_uart.h"
#include "debug/klog.h"

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
    hal_uart_init(0);

    if (prefix && *prefix) {
        klog_puts(prefix);
    } else {
        klog_puts("PANIC: ");
    }

    if (msg && *msg) {
        klog_puts(msg);
    } else {
        klog_puts("<no message>");
    }

    klog_puts("\n");

    park_cpu();
}

__attribute__((noreturn))
void panic(const char *msg)
{
    panic_with_prefix("PANIC: ", msg);
}
