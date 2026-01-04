#include "MathHelper.h"

/* Minimal, freestanding helpers (no libc). */

static size_t mh_append(char *dst, size_t dst_sz, size_t off, const char *src)
{
    if (!dst || dst_sz == 0 || off >= dst_sz) {
        return off;
    }
    size_t i = 0;
    while (src && src[i] && off + 1 < dst_sz) {
        dst[off++] = src[i++];
    }
    dst[off] = '\0';
    return off;
}

static size_t mh_append_u64(char *dst, size_t dst_sz, size_t off, uint64_t v)
{
    /* Convert into a temporary buffer in reverse. */
    char tmp[32];
    size_t len = 0;

    if (v == 0) {
        return mh_append(dst, dst_sz, off, "0");
    }

    while (v != 0 && len < sizeof(tmp)) {
        uint64_t digit = v % 10;
        tmp[len++] = (char)('0' + (char)digit);
        v /= 10;
    }

    /* Append back in correct order. */
    while (len != 0 && off + 1 < dst_sz) {
        dst[off++] = tmp[--len];
    }
    if (dst_sz > 0) {
        dst[off < dst_sz ? off : (dst_sz - 1)] = '\0';
    }
    return off;
}

size_t mh_format_bytes_pretty(char *out, size_t out_sz, uint64_t bytes)
{
    static const char *kUnits[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    const size_t kMaxUnit = (sizeof(kUnits) / sizeof(kUnits[0])) - 1;

    if (!out || out_sz == 0) {
        return 0;
    }
    out[0] = '\0';

    /* Pick the largest 1024-based unit that keeps the integer part >= 1. */
    uint64_t unit_div = 1;
    size_t unit_idx = 0;
    while (unit_idx < kMaxUnit) {
        if (unit_div > (UINT64_MAX / 1024ULL)) {
            break;
        }
        uint64_t next = unit_div * 1024ULL;
        if (bytes < next) {
            break;
        }
        unit_div = next;
        unit_idx++;
    }

    uint64_t whole = (unit_div == 0) ? 0 : (bytes / unit_div);
    uint64_t rem = (unit_div == 0) ? 0 : (bytes % unit_div);

    size_t off = 0;
    off = mh_append_u64(out, out_sz, off, whole);

    /* Optional single decimal place for small values (e.g. 1.5KB). */
    if (unit_idx != 0 && whole < 10 && rem != 0 && off + 2 < out_sz) {
        /*
         * Keep this strictly 64-bit so we don't pull in compiler-rt 128-bit
         * division helpers (e.g. __udivti3) in a freestanding kernel.
         *
         * rem < unit_div and unit_div <= 1024^6 == 2^60, so (rem * 10)
         * is safely within uint64_t.
         */
        uint64_t tmp = rem * 10u + (unit_div / 2u);
        uint64_t tenth = tmp / unit_div;

        if (tenth >= 10) {
            /* Carry from rounding (9.95 -> 10.0). */
            whole += 1;
            tenth = 0;
            out[0] = '\0';
            off = mh_append_u64(out, out_sz, 0, whole);
        }

        if (tenth != 0 && off + 2 < out_sz) {
            out[off++] = '.';
            out[off++] = (char)('0' + (char)tenth);
            out[off] = '\0';
        }
    }

    off = mh_append(out, out_sz, off, kUnits[unit_idx]);
    return off;
}

///*
// * Optional exported alias for older call sites that may not include the header.
// * Keep this as a real symbol to avoid link errors if referenced.
// */
//size_t mh_format_pretty_size(char *out, size_t out_sz, uint64_t bytes)
//{
//    return mh_format_bytes_pretty(out, out_sz, bytes);
//}
