/*
 * core_abi_v1.c
 *
 * Kernel Services ABI v1 implementation.
 * This file is kernel-private; Core sees only the ABI header.
 */

#include "core_kernel_abi.h"

#include "uart_pl011.h"
#include "panic.h"
#include "kheap.h"
#include "irq.h"
#include "timer_generic.h"
#include "sched.h"

static void ks_log(const char *msg) {
    if (msg) {
        uart_puts(msg);
    }
    uart_putnl();
}

static void ks_panic(const char *msg) {
    panic(msg ? msg : "panic");
}

static void *ks_alloc(size_t size) {
    /*
     * Core's allocation surface is explicitly a BUFFER allocator.
     * Kernel OBJECTS must not use this path; they should use slab caches.
     */
    return kbuf_alloc(size);
}

static void ks_free(void *ptr) {
    kbuf_free(ptr);
}

static uint64_t ks_irq_save(void) {
    return irq_save();
}

static void ks_irq_restore(uint64_t prev_daif) {
    irq_restore(prev_daif);
}

static uint64_t ks_time_now_ticks(void) {
    return timer_ticks_read();
}

static void ks_yield(void) {
    yield();
}

static const kernel_services_v1_t g_kernel_services_v1 = {
    .abi_version    = KERNEL_SERVICES_ABI_VERSION,
    .log            = ks_log,
    .panic          = ks_panic,
    .alloc          = ks_alloc,
    .free           = ks_free,
    .irq_save       = ks_irq_save,
    .irq_restore    = ks_irq_restore,
    .time_now_ticks = ks_time_now_ticks,
    .yield          = ks_yield,
};

const kernel_services_v1_t *kernel_services_v1(void) {
    return &g_kernel_services_v1;
}
