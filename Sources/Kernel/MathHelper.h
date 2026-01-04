#ifndef CAPAZ_MATH_HELPER_H
#define CAPAZ_MATH_HELPER_H

#include <stdint.h>
#include <stddef.h>

/*
 * Formats a byte count into a human-friendly string using 1024-based units.
 *
 * Examples:
 *   128 * 1024 * 1024 -> "128MB"
 *   1536              -> "1.5KB"
 *
 * The result is always NUL-terminated if out_sz > 0.
 *
 * Returns the number of characters written (excluding the terminating NUL).
 */
/* Preferred API name (used by kmain). */
size_t mh_format_bytes_pretty(char *out, size_t out_sz, uint64_t bytes);

/* Backwards-compatible alias. */
static inline size_t mh_format_pretty_size(char *out, size_t out_sz, uint64_t bytes) {
    return mh_format_bytes_pretty(out, out_sz, bytes);
}

#endif /* CAPAZ_MATH_HELPER_H */
