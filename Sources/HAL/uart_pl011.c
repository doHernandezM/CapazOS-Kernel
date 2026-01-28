#include "uart_pl011.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define HH_PHYS_BASE 0xFFFF800000000000ULL
#define UART_FALLBACK_PHYS_BASE 0x09000000ULL

/* PL011 register offsets */
#define UARTDR      0x00
#define UARTRSR     0x04   /* Read: Receive Status */
#define UARTECR     0x04   /* Write: Error Clear (same offset) */
#define UARTFR      0x18
#define UARTIBRD    0x24
#define UARTFBRD    0x28
#define UARTLCR_H   0x2C
#define UARTCR      0x30
#define UARTIFLS    0x34
#define UARTIMSC    0x38
#define UARTRIS     0x3C
#define UARTMIS     0x40
#define UARTICR     0x44

/* UARTFR bits */
#define UARTFR_BUSY (1u << 3)
#define UARTFR_RXFE (1u << 4)
#define UARTFR_TXFF (1u << 5)

/* UARTCR bits */
#define UARTCR_UARTEN (1u << 0)
#define UARTCR_TXE    (1u << 8)
#define UARTCR_RXE    (1u << 9)

/* UARTIMSC bits */
#define UARTIMSC_RXIM (1u << 4)
#define UARTIMSC_RTIM (1u << 6)

/* UARTICR bits */
#define UARTICR_RXIC  (1u << 4)
#define UARTICR_RTIC  (1u << 6)

/* UARTLCR_H bits */
#define UARTLCRH_BRK   (1u << 0)
#define UARTLCRH_PEN   (1u << 1)
#define UARTLCRH_EPS   (1u << 2)
#define UARTLCRH_STP2  (1u << 3)
#define UARTLCRH_FEN   (1u << 4)
#define UARTLCRH_WLEN_8 (3u << 5) /* 8-bit words */

/* UARTRSR / DR error bits */
#define UARTERR_FE (1u << 0) /* Framing */
#define UARTERR_PE (1u << 1) /* Parity */
#define UARTERR_BE (1u << 2) /* Break */
#define UARTERR_OE (1u << 3) /* Overrun */

/* UARTICR: writing 1s clears corresponding interrupts.
   0x7FF is commonly used to clear "all" PL011 interrupt sources. */
#define UARTICR_ALL 0x7FFu

static volatile uint32_t *g_uart_base =
    (volatile uint32_t *)(HH_PHYS_BASE + UART_FALLBACK_PHYS_BASE);

static inline void mmio_write(uint32_t off, uint32_t v) {
    g_uart_base[off / 4] = v;
}
static inline uint32_t mmio_read(uint32_t off) {
    return g_uart_base[off / 4];
}

void uart_init(uint64_t uart_phys_base)
{
    if (uart_phys_base != 0) {
        g_uart_base = (volatile uint32_t *)(HH_PHYS_BASE + uart_phys_base);
    }
}

/* Optional: explicit hardware init (polled, no interrupts required).
   You must supply the UART reference clock in Hz for accurate baud.
   If clock_hz == 0, we skip baud programming and just enable 8N1 + FIFO. */
void uart_hw_init(uint32_t clock_hz, uint32_t baud)
{
    /* Disable UART before reconfig */
    mmio_write(UARTCR, 0);

    /* Wait until not busy (best-effort; avoids truncating TX) */
    while (mmio_read(UARTFR) & UARTFR_BUSY) {
        __asm__ volatile("nop");
    }

    /* Clear any pending interrupts and latched errors */
    mmio_write(UARTICR, UARTICR_ALL);
    mmio_write(UARTECR, 0xFF); /* clears FE/PE/BE/OE latches (write any value) */

    /* Program baud rate divisors if clock known */
    if (clock_hz != 0 && baud != 0) {
        /* BRD = UARTCLK / (16 * baud)
           IBRD = floor(BRD)
           FBRD = round((BRD - IBRD) * 64)
           Implemented with integer arithmetic. */
        uint64_t denom = 16ull * (uint64_t)baud;
        uint64_t ibrd  = (uint64_t)clock_hz / denom;
        uint64_t rem   = (uint64_t)clock_hz % denom;

        /* fractional = round(rem * 64 / denom) */
        uint64_t fbrd  = (rem * 64ull + (denom / 2ull)) / denom;

        /* Per PL011 expectations, FBRD is 6 bits (0..63).
           If rounding yields 64, carry into IBRD. */
        if (fbrd >= 64ull) {
            ibrd += 1ull;
            fbrd = 0ull;
        }

        mmio_write(UARTIBRD, (uint32_t)ibrd);
        mmio_write(UARTFBRD, (uint32_t)fbrd);
    }

    /* 8N1, enable FIFOs (optional but typically desirable even in bring-up) */
    uint32_t lcrh = UARTLCRH_WLEN_8 | UARTLCRH_FEN;
    /* parity disabled, 1 stop bit, no break */
    mmio_write(UARTLCR_H, lcrh);

    /* Enable UART, TX, RX */
    mmio_write(UARTCR, UARTCR_UARTEN | UARTCR_TXE | UARTCR_RXE);
}

void uart_putc(char c)
{
    while (mmio_read(UARTFR) & UARTFR_TXFF) {
        __asm__ volatile("nop");
    }
    mmio_write(UARTDR, (uint32_t)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_putnl(void) { uart_puts("\n"); }

/* --- Polled RX support --- */

/* Returns true if a byte is available to read */
bool uart_rx_ready(void)
{
    return (mmio_read(UARTFR) & UARTFR_RXFE) == 0;
}

/* Non-blocking getc:
   - returns true and sets *out if a char was read
   - returns false if RX FIFO empty
   - clears any error latches if the read char had errors */
bool uart_getc_nonblock(char *out)
{
    if (!uart_rx_ready())
        return false;

    uint32_t dr = mmio_read(UARTDR);

    /* Low 8 bits are data. Bits [11:8] reflect error for this character. */
    uint32_t err = (dr >> 8) & 0x0Fu;
    if (err) {
        /* Clear latched error state to avoid sticky conditions. */
        mmio_write(UARTECR, 0xFF);
        /* You can choose to drop errored characters; here we still return it. */
    }

    *out = (char)(dr & 0xFFu);
    return true;
}

/* Blocking getc */
char uart_getc(void)
{
    char c;
    while (!uart_getc_nonblock(&c)) {
        __asm__ volatile("nop");
    }
    return c;
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

// uart_pl011.c (add implementation, e.g. after uart_puthex64)

void uart_putu64_dec(uint64_t value) {
    /* Max uint64_t is 18446744073709551615 (20 digits) */
    char buf[21];
    int i = 0;

    if (value == 0) {
        uart_putc('0');
        return;
    }

    while (value != 0) {
        uint64_t q = value / 10ULL;
        uint64_t r = value - (q * 10ULL);
        buf[i++] = (char)('0' + (char)r);
        value = q;
    }

    while (i--) {
        uart_putc(buf[i]);
    }
}


void uart_enable_rx_irq(void)
{
    /* Clear any pending UART interrupts. */
    mmio_write(UARTICR, UARTICR_ALL);

    /* Enable RX and RX timeout interrupts. */
    uint32_t mask = mmio_read(UARTIMSC);
    mask |= (UARTIMSC_RXIM | UARTIMSC_RTIM);
    mmio_write(UARTIMSC, mask);
}

void uart_disable_rx_irq(void)
{
    uint32_t mask = mmio_read(UARTIMSC);
    mask &= ~(UARTIMSC_RXIM | UARTIMSC_RTIM);
    mmio_write(UARTIMSC, mask);

    /* Clear any pending UART interrupts. */
    mmio_write(UARTICR, UARTICR_ALL);
}

bool uart_irq_drain_rx(char *out_buf, size_t max, size_t *out_n)
{
    if (out_n) *out_n = 0;
    if (!out_buf || max == 0) return false;

    size_t n = 0;
    while (n < max && uart_rx_ready()) {
        uint32_t dr = mmio_read(UARTDR);
        uint32_t err = (dr >> 8) & 0x0Fu;
        if (err) {
            mmio_write(UARTECR, 0xFF);
        }
        out_buf[n++] = (char)(dr & 0xFFu);
    }

    /* Ack RX-related sources (best-effort). */
    mmio_write(UARTICR, (UARTICR_RXIC | UARTICR_RTIC));

    if (out_n) *out_n = n;
    return n != 0;
}
