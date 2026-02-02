#include "debug/klog.h"

#include "api/core_kernel_api.h"

static void klog_write_bytes(const char *s, size_t len)
{
    if (!s || len == 0) {
        return;
    }
    cka_console_write(s, len);
}

void klog_write(const char *s, size_t len)
{
    klog_write_bytes(s, len);
}

void klog_puts(const char *s)
{
    if (!s) {
        return;
    }
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    klog_write_bytes(s, len);
}

void klog_putc(char c)
{
    klog_write_bytes(&c, 1);
}

void klog_putnl(void)
{
    const char nl = '\n';
    klog_write_bytes(&nl, 1);
}

void klog_puthex64(uint64_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[18];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[17 - i] = hex[value & 0xF];
        value >>= 4;
    }
    klog_write_bytes(buf, sizeof(buf));
}

void klog_putu64_dec(uint64_t value)
{
    char buf[21];
    int i = 0;

    if (value == 0) {
        klog_putc('0');
        return;
    }

    while (value != 0) {
        uint64_t q = value / 10ULL;
        uint64_t r = value - (q * 10ULL);
        buf[i++] = (char)('0' + (char)r);
        value = q;
    }

    while (i--) {
        klog_putc(buf[i]);
    }
}
