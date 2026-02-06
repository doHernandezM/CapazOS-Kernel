#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Minimal loader HAL (board shim).

void boot_uart_putc(char c);
void boot_uart_puts(const char *s);

// Read `count` 512-byte sectors at LBA into `buf`.
bool boot_block_read(uint64_t lba, uint32_t count, void *buf);

// Optional timer (returns ticks, or 0 if unsupported).
uint64_t boot_time_now(void);

// Optional DTB pointer from firmware/boot protocol (physical address or NULL).
const void *boot_dtb_ptr(void);
