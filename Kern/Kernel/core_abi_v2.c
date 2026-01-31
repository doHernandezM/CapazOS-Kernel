#include "core_kernel_abi_v2.h"

#include "cap/cap_ops.h"
#include "cap/cap_rights.h"
#include "contracts.h"
// IRQ API lives under Sources/Kernel/irg and is exported via the kernel include path.
// Other kernel code includes it as "irq.h".
#include "irq.h"
#include "kheap.h"
#include "panic.h"
// UART driver header is exported on the kernel include path (see other users of "uart_pl011.h").
#include "uart_pl011.h"
#include "sched/sched.h"
// Timer API header is exported on the include path as "timer_generic.h".
#include "timer_generic.h"
#include "task/task.h"
#include "cap/cap_status_ks.h"

// Map kernel cap_status_t to ABI ks_cap_status_t values.
static ks_cap_status_t ks_from_cap_status(cap_status_t st)
{
    return cap_status_to_ks_status(st);
}

static cap_table_t *current_caps(void) {
    thread_t *cur = sched_current();
    if (!cur || !cur->task) {
        return NULL;
    }
    return cur->task->caps;
}

static ks_cap_status_t ks_cap_dup_impl(ks_cap_handle_t h,
                                       ks_cap_rights_t mask,
                                       ks_cap_handle_t *out) {
    if (!out) {
        return KS_CAP_ERR_INVALID;
    }
    cap_table_t *t = current_caps();
    if (!t) {
        return KS_CAP_ERR_INVALID;
    }
    cap_handle_t new_h = 0;
    cap_status_t s = cap_dup(t, (cap_handle_t)h, t, (cap_rights_t)mask, &new_h);
    *out = (ks_cap_handle_t)new_h;
    return ks_from_cap_status(s);
}

static ks_cap_status_t ks_cap_transfer_impl(ks_cap_handle_t h,
                                            ks_cap_rights_t mask,
                                            ks_cap_handle_t *out) {
    if (!out) {
        return KS_CAP_ERR_INVALID;
    }
    cap_table_t *t = current_caps();
    if (!t) {
        return KS_CAP_ERR_INVALID;
    }
    cap_handle_t new_h = 0;
    cap_status_t s = cap_transfer(t, (cap_handle_t)h, t, (cap_rights_t)mask, &new_h);
    *out = (ks_cap_handle_t)new_h;
    return ks_from_cap_status(s);
}

static ks_cap_status_t ks_cap_drop_impl(ks_cap_handle_t h) {
    cap_table_t *t = current_caps();
    if (!t) {
        return KS_CAP_ERR_INVALID;
    }
    return ks_from_cap_status(cap_drop(t, (cap_handle_t)h));
}

static ks_cap_status_t ks_cap_invalidate_impl(ks_cap_handle_t h) {
    cap_table_t *t = current_caps();
    if (!t) {
        return KS_CAP_ERR_INVALID;
    }
    return ks_from_cap_status(cap_invalidate(t, (cap_handle_t)h));
}

// v2 extends v1; keep these semantics identical to v1.
static void ks_log(const char *s) {
    if (!s) {
        return;
    }
    uart_puts(s);
    uart_putc('\n');
}

static void *ks_alloc(size_t size) {
    ASSERT_THREAD_CONTEXT();
    return kmalloc(size);
}

static void ks_free(void *ptr) {
    ASSERT_THREAD_CONTEXT();
    kfree(ptr);
}

static void ks_yield(void) {
    ASSERT_THREAD_CONTEXT();
    yield();
}

static const kernel_services_v2_t g_kernel_services_v2 = {
    .abi_version = 2,
    .reserved0 = 0,
    .log = ks_log,
    .alloc = ks_alloc,
    .free = ks_free,
    .yield = ks_yield,

    .cap_dup = ks_cap_dup_impl,
    .cap_transfer = ks_cap_transfer_impl,
    .cap_drop = ks_cap_drop_impl,
    .cap_invalidate = ks_cap_invalidate_impl,
};

const kernel_services_v2_t *kernel_services_v2(void) {
    return &g_kernel_services_v2;
}
