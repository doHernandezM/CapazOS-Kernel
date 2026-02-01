/*
 * core_kernel_api.h
 *
 * Internal API for Core ↔ Kernel interactions. This header declares
 * kernel mechanisms that are safe for Core to call. Unlike the old
 * versioned ABI tables, these functions are linked directly and do not
 * require runtime service injection. All functions must be POD and
 * freestanding (no reliance on C runtime). Keep the API minimal: only
 * expose what Core needs for bring‑up and gradually extend as
 * functionality grows.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Logging: write a null‑terminated string to the early console. This is
 * safe in early boot and may be routed to the UART until a console
 * service is registered. This function is thread‑context only (no
 * allocation). Passing NULL does nothing. */
void cka_log_write(const char *str);

/* Allocate a buffer with the given size and alignment. The returned
 * pointer is guaranteed to be aligned to at least the supplied
 * alignment (which must be a power of two). Passing an alignment of
 * zero defaults to max_align_t alignment. On failure returns NULL.
 * Allocation is thread‑context only. */
void *cka_malloc(size_t size, size_t alignment);

/* Free a buffer previously returned by cka_malloc or related APIs. If
 * ptr is NULL, no action is taken. Thread‑context only. */
void cka_free(void *ptr);

/* Memory copy and set helpers. These provide a minimal memcpy/memset
 * implementation so Core does not need libc. They return the
 * destination pointer. */
void *cka_memcpy(void *dst, const void *src, size_t n);
void *cka_memset(void *ptr, int value, size_t n);

/* Yield the CPU voluntarily. This cooperatively yields the current
 * thread so that other runnable threads may run. It is safe only in
 * thread context. */
void cka_yield(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
