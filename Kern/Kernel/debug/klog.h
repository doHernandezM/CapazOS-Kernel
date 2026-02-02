#pragma once

#include <stddef.h>
#include <stdint.h>

void klog_write(const char *s, size_t len);
void klog_puts(const char *s);
void klog_putc(char c);
void klog_putnl(void);
void klog_puthex64(uint64_t value);
void klog_putu64_dec(uint64_t value);
