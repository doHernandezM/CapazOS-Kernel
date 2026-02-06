#include "core_kernel_api.h"

#include "kheap.h"
#include "cap/cap_ops.h"
#include "mm/mem.h"
#include "sched/sched.h"
#include "serial/uart.h"
#include "api/kernel_log_proto.h"
#include "platform/dtb.h"
#include "buildinfo.h"
#include "hal_block.h"
#include "task/task.h"
#include "sched/thread.h"
#include "config.h"

static cap_table_t *g_core_caps;
static cap_handle_t g_kernel_log_ep = CAP_HANDLE_INVALID;

typedef struct ks_contract_entry {
    uint32_t rights;
    uint32_t policy;
    uint8_t in_use;
} ks_contract_entry_t;

static ks_contract_entry_t g_contracts[64];

typedef struct core_alloc_hdr {
    uint64_t magic;
    uint64_t size;
} core_alloc_hdr_t;

#define CORE_ALLOC_MAGIC 0x434150415A4D454DULL

static task_t *cka_current_task(void)
{
    thread_t *cur = sched_current();
    if (!cur) {
        return NULL;
    }
    return cur->task;
}

static uint32_t cka_sched_intent_from_io(uint32_t io_intent)
{
    switch (io_intent) {
        case KS_IO_INTENT_LATENCY:
            return (uint32_t)SCHED_INTENT_LATENCY;
        case KS_IO_INTENT_THROUGHPUT:
            return (uint32_t)SCHED_INTENT_THROUGHPUT;
        case KS_IO_INTENT_BACKGROUND:
            return (uint32_t)SCHED_INTENT_BACKGROUND;
        default:
            return (uint32_t)SCHED_INTENT_INTERACTIVE;
    }
}

void cka_attach_core_caps(cap_table_t *core_caps)
{
    g_core_caps = core_caps;
}

cap_table_t *cka_core_caps(void)
{
    return g_core_caps;
}

void cka_attach_kernel_log_ep(cap_id_t endpoint_cap)
{
    g_kernel_log_ep = (cap_handle_t)endpoint_cap;
}

// ---- Logging ----

void cka_early_log(const char *s)
{
    if (!s) {
        return;
    }
    cka_console_write(s, strlen(s));
}

void cka_console_write(const char *s, size_t len)
{
    if (!s || len == 0) {
        return;
    }
    if (!g_core_caps || g_kernel_log_ep == CAP_HANDLE_INVALID) {
        uart_write(s, len);
        return;
    }

    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > KS_IPC_MSG_MAX) {
            chunk = KS_IPC_MSG_MAX;
        }

        ks_ipc_msg_t msg;
        msg.tag = KLOG_TAG_WRITE;
        msg.len = (uint32_t)chunk;
        memcpy(msg.data, s + off, chunk);
        ks_ipc_status_t st = ipc_send_cap(g_core_caps, g_kernel_log_ep, &msg);
        if (st != KS_IPC_OK) {
            uart_write(s + off, len - off);
            return;
        }
        off += chunk;
    }
}

void cka_log_write(const char *s)
{
    if (!s) {
        return;
    }
    cka_console_write(s, strlen(s));
}

ks_status_t cka_get_build_info(ks_build_info_t *out_info)
{
    if (!out_info) {
        return KS_STATUS_INVALID_ARG;
    }
    out_info->kernel_build_number = (uint64_t)CAPAZ_KERNEL_BUILD_NUMBER;
    out_info->build_date = CAPAZ_BUILD_DATE;
    out_info->build_version = CAPAZ_BUILD_VERSION;
    out_info->build_environment = CAPAZ_BUILD_ENVIRONMENT;
    out_info->kernel_version = CAPAZ_KERNEL_VERSION;
    out_info->kernel_machine = CAPAZ_MACHINE;
    out_info->core_name = CAPAZ_CORE_NAME;
    out_info->core_version = CAPAZ_CORE_VERSION;
    return KS_STATUS_OK;
}

ks_status_t cka_block_get_info(ks_block_info_t *out_info)
{
    if (!out_info) {
        return KS_STATUS_INVALID_ARG;
    }
    if (!hal_block_ready()) {
        return KS_STATUS_NOT_SUPPORTED;
    }
    out_info->sector_size = hal_block_sector_size();
    out_info->capacity_sectors = hal_block_capacity_sectors();
    return KS_STATUS_OK;
}

ks_status_t cka_block_read(uint64_t lba, uint32_t count, void *buf, size_t buf_len)
{
    return cka_block_read_intent(lba, count, buf, buf_len, KS_IO_INTENT_LATENCY);
}

ks_status_t cka_block_read_intent(uint64_t lba, uint32_t count, void *buf, size_t buf_len, uint32_t intent)
{
    ks_status_t status = KS_STATUS_OK;

    if (!buf || count == 0) {
        return KS_STATUS_INVALID_ARG;
    }
    if (!hal_block_ready()) {
        return KS_STATUS_NOT_SUPPORTED;
    }
    uint64_t need = (uint64_t)count * (uint64_t)hal_block_sector_size();
    if ((uint64_t)buf_len < need) {
        return KS_STATUS_INVALID_ARG;
    }

    task_t *task = cka_current_task();
#if CONFIG_RES_CEILINGS
    if (task && !task_allow_io(task, need, false, sched_ticks_now())) {
        return KS_STATUS_BUSY;
    }
#endif

    thread_t *cur = sched_current();
    uint32_t old_intent = cur ? (uint32_t)cur->sched_intent : (uint32_t)SCHED_INTENT_INTERACTIVE;
    if (cur) {
        sched_set_thread_intent(cur, cka_sched_intent_from_io(intent));
    }

    uint32_t max_chunk = count;
    if (intent == KS_IO_INTENT_THROUGHPUT) {
        max_chunk = 8;
    } else if (intent == KS_IO_INTENT_BACKGROUND) {
        max_chunk = 1;
    }

    uint32_t remaining = count;
    uint32_t offset_sectors = 0;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t sector_size = hal_block_sector_size();

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (chunk > max_chunk) {
            chunk = max_chunk;
        }
        uint64_t chunk_bytes = (uint64_t)chunk * (uint64_t)sector_size;
        uint64_t pages = (chunk_bytes + 0xFFFULL) / 0x1000ULL;
        uint64_t tmp_pa = 0;
        void *tmp = kheap_alloc_pages((uint32_t)pages, &tmp_pa);
        if (!tmp) {
            status = KS_STATUS_OUT_OF_MEMORY;
            break;
        }
        if (!hal_block_read(lba + offset_sectors, tmp, chunk)) {
            kheap_free_pages(tmp, (uint32_t)pages);
            status = KS_STATUS_INTERNAL;
            break;
        }
        memcpy(dst + ((uint64_t)offset_sectors * (uint64_t)sector_size), tmp, (size_t)chunk_bytes);
        kheap_free_pages(tmp, (uint32_t)pages);

        remaining -= chunk;
        offset_sectors += chunk;

        if (intent == KS_IO_INTENT_BACKGROUND) {
            /* Throttle background reads. */
            for (uint32_t i = 0; i < 2000; i++) {
                yield();
            }
        }
    }

    if (status == KS_STATUS_OK && task) {
        task_account_io(task, need, false, cka_sched_intent_from_io(intent), sched_ticks_now());
    }
    if (cur) {
        sched_set_thread_intent(cur, old_intent);
    }
    return status;
}

ks_status_t cka_block_write(uint64_t lba, uint32_t count, const void *buf, size_t buf_len)
{
    return cka_block_write_intent(lba, count, buf, buf_len, KS_IO_INTENT_LATENCY);
}

ks_status_t cka_block_write_intent(uint64_t lba, uint32_t count, const void *buf, size_t buf_len, uint32_t intent)
{
    ks_status_t status = KS_STATUS_OK;

    if (!buf || count == 0) {
        return KS_STATUS_INVALID_ARG;
    }
    if (!hal_block_ready()) {
        return KS_STATUS_NOT_SUPPORTED;
    }
    uint64_t need = (uint64_t)count * (uint64_t)hal_block_sector_size();
    if ((uint64_t)buf_len < need) {
        return KS_STATUS_INVALID_ARG;
    }

    task_t *task = cka_current_task();
#if CONFIG_RES_CEILINGS
    if (task && !task_allow_io(task, need, true, sched_ticks_now())) {
        return KS_STATUS_BUSY;
    }
#endif

    thread_t *cur = sched_current();
    uint32_t old_intent = cur ? (uint32_t)cur->sched_intent : (uint32_t)SCHED_INTENT_INTERACTIVE;
    if (cur) {
        sched_set_thread_intent(cur, cka_sched_intent_from_io(intent));
    }

    uint32_t max_chunk = count;
    if (intent == KS_IO_INTENT_THROUGHPUT) {
        max_chunk = 8;
    } else if (intent == KS_IO_INTENT_BACKGROUND) {
        max_chunk = 1;
    }

    uint32_t remaining = count;
    uint32_t offset_sectors = 0;
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t sector_size = hal_block_sector_size();

    while (remaining > 0) {
        uint32_t chunk = remaining;
        if (chunk > max_chunk) {
            chunk = max_chunk;
        }
        uint64_t chunk_bytes = (uint64_t)chunk * (uint64_t)sector_size;
        uint64_t pages = (chunk_bytes + 0xFFFULL) / 0x1000ULL;
        uint64_t tmp_pa = 0;
        void *tmp = kheap_alloc_pages((uint32_t)pages, &tmp_pa);
        if (!tmp) {
            status = KS_STATUS_OUT_OF_MEMORY;
            break;
        }
        memcpy(tmp, src + ((uint64_t)offset_sectors * (uint64_t)sector_size), (size_t)chunk_bytes);
        if (!hal_block_write(lba + offset_sectors, tmp, chunk)) {
            kheap_free_pages(tmp, (uint32_t)pages);
            status = KS_STATUS_INTERNAL;
            break;
        }
        kheap_free_pages(tmp, (uint32_t)pages);

        remaining -= chunk;
        offset_sectors += chunk;

        if (intent == KS_IO_INTENT_BACKGROUND) {
            for (uint32_t i = 0; i < 2000; i++) {
                yield();
            }
        }
    }

    if (status == KS_STATUS_OK && task) {
        task_account_io(task, need, true, cka_sched_intent_from_io(intent), sched_ticks_now());
    }
    if (cur) {
        sched_set_thread_intent(cur, old_intent);
    }
    return status;
}

// ---- Contracts ----

ks_status_t cka_contract_open(uint32_t kind, uint32_t rights, uint32_t policy, uint64_t *out_handle)
{
    if (!out_handle) {
        return KS_STATUS_INVALID_ARG;
    }
    if (kind != KS_CONTRACT_FAT32) {
        return KS_STATUS_INVALID_ARG;
    }
    for (uint32_t i = 0; i < (uint32_t)(sizeof(g_contracts) / sizeof(g_contracts[0])); i++) {
        if (!g_contracts[i].in_use) {
            g_contracts[i].in_use = 1;
            g_contracts[i].rights = rights;
            g_contracts[i].policy = policy;
            *out_handle = (uint64_t)(i + 1);
            return KS_STATUS_OK;
        }
    }
    return KS_STATUS_OUT_OF_MEMORY;
}

ks_status_t cka_contract_get(uint64_t handle, uint32_t *out_rights, uint32_t *out_policy)
{
    if (!out_rights || !out_policy) {
        return KS_STATUS_INVALID_ARG;
    }
    if (handle == 0) {
        return KS_STATUS_INVALID_ARG;
    }
    uint64_t idx = handle - 1;
    if (idx >= (uint64_t)(sizeof(g_contracts) / sizeof(g_contracts[0]))) {
        return KS_STATUS_INVALID_ARG;
    }
    if (!g_contracts[idx].in_use) {
        return KS_STATUS_INVALID_ARG;
    }
    *out_rights = g_contracts[idx].rights;
    *out_policy = g_contracts[idx].policy;
    return KS_STATUS_OK;
}

static ks_status_t ks_from_ipc_status(ks_ipc_status_t st)
{
    switch (st) {
        case KS_IPC_OK:          return KS_STATUS_OK;
        case KS_IPC_ERR_INVALID: return KS_STATUS_INVALID_ARG;
        case KS_IPC_ERR_RIGHTS:  return KS_STATUS_NO_RIGHTS;
        case KS_IPC_ERR_NO_MEM:  return KS_STATUS_OUT_OF_MEMORY;
        case KS_IPC_ERR_CLOSED:  return KS_STATUS_BUSY;
        case KS_IPC_ERR_EMPTY:   return KS_STATUS_BUSY;
        default:                 return KS_STATUS_INVALID_ARG;
    }
}

// ---- Memory ----

void *cka_kmalloc(size_t size)
{
    size_t total = size + sizeof(core_alloc_hdr_t);
    task_t *task = cka_current_task();
#if CONFIG_RES_CEILINGS
    if (task && !task_allow_mem_alloc(task, (uint64_t)size)) {
        return NULL;
    }
#endif

    core_alloc_hdr_t *hdr = (core_alloc_hdr_t *)kmalloc(total);
    if (!hdr) {
        return NULL;
    }
    hdr->magic = CORE_ALLOC_MAGIC;
    hdr->size = (uint64_t)size;
    if (task) {
        task_account_mem_alloc(task, (uint64_t)size);
    }
    return (void *)(hdr + 1);
}

void cka_kfree(void *ptr)
{
    if (!ptr) {
        return;
    }

    core_alloc_hdr_t *hdr = ((core_alloc_hdr_t *)ptr) - 1;
    if (hdr->magic == CORE_ALLOC_MAGIC) {
        task_t *task = cka_current_task();
        if (task) {
            task_account_mem_free(task, hdr->size);
        }
        hdr->magic = 0;
        kfree(hdr);
        return;
    }

    // Fallback for legacy pointers not wrapped by cka_kmalloc.
    kfree(ptr);
}

static size_t round_up_pow2_size(size_t x)
{
    if (x <= 1) {
        return 1;
    }
    // Round up to the next power of two.
    size_t p = 1;
    while (p < x) {
        p <<= 1;
    }
    return p;
}

void *cka_malloc(size_t size, size_t alignment)
{
    if (alignment < sizeof(void *)) {
        alignment = sizeof(void *);
    }
    alignment = round_up_pow2_size(alignment);

    // We store the original pointer in the word immediately before the
    // returned aligned pointer.
    size_t extra = (alignment - 1) + sizeof(void *);
    void *raw = cka_kmalloc(size + extra);
    if (!raw) {
        return NULL;
    }

    uintptr_t base = (uintptr_t)raw + sizeof(void *);
    uintptr_t aligned = (base + (alignment - 1)) & ~(uintptr_t)(alignment - 1);
    ((void **)aligned)[-1] = raw;
    return (void *)aligned;
}

void cka_free(void *ptr)
{
    if (!ptr) {
        return;
    }
    void *raw = ((void **)ptr)[-1];
    cka_kfree(raw);
}

void *cka_memcpy(void *dst, const void *src, size_t n)
{
    return memcpy(dst, src, n);
}

void *cka_memset(void *dst, int c, size_t n)
{
    return memset(dst, c, n);
}

// ---- Scheduling ----

void cka_yield(void)
{
    yield();
}

void cka_sleep_ticks(uint64_t ticks)
{
    while (ticks--) {
        yield();
    }
}

ks_status_t cka_sched_set_contract(const ks_sched_contract_t *contract)
{
    if (!contract) {
        return KS_STATUS_INVALID_ARG;
    }
    thread_t *cur = sched_current();
    if (!cur || !cur->task) {
        return KS_STATUS_INVALID_STATE;
    }

    task_contract_t tc = {
        .intent = contract->intent,
        .flags = contract->flags,
        .window_ticks = contract->window_ticks,
        .cpu_ticks_limit = contract->cpu_ticks_limit,
        .io_read_bytes_limit = contract->io_read_bytes_limit,
        .io_write_bytes_limit = contract->io_write_bytes_limit,
        .mem_bytes_limit = contract->mem_bytes_limit,
    };
    task_set_contract(cur->task, &tc, sched_ticks_now());
    sched_set_thread_intent(cur, tc.intent);
    return KS_STATUS_OK;
}

ks_status_t cka_sched_get_contract(ks_sched_contract_t *out_contract)
{
    if (!out_contract) {
        return KS_STATUS_INVALID_ARG;
    }
    thread_t *cur = sched_current();
    if (!cur || !cur->task) {
        return KS_STATUS_INVALID_STATE;
    }

    task_contract_t tc = {0};
    task_get_contract(cur->task, &tc);
    out_contract->intent = tc.intent;
    out_contract->flags = tc.flags;
    out_contract->window_ticks = tc.window_ticks;
    out_contract->cpu_ticks_limit = tc.cpu_ticks_limit;
    out_contract->io_read_bytes_limit = tc.io_read_bytes_limit;
    out_contract->io_write_bytes_limit = tc.io_write_bytes_limit;
    out_contract->mem_bytes_limit = tc.mem_bytes_limit;
    return KS_STATUS_OK;
}

ks_status_t cka_sched_get_stats(ks_sched_stats_t *out_stats)
{
    if (!out_stats) {
        return KS_STATUS_INVALID_ARG;
    }
    thread_t *cur = sched_current();
    if (!cur || !cur->task) {
        return KS_STATUS_INVALID_STATE;
    }

    sched_stats_t ss = {0};
    sched_get_stats(&ss);

    task_stats_t ts = {0};
    task_get_stats(cur->task, ss.tick_now, &ts);

    out_stats->tick_now = ts.tick_now;
    out_stats->window_start_tick = ts.window_start_tick;
    out_stats->window_ticks = ts.window_ticks;

    out_stats->cpu_ticks_total = ts.cpu_ticks_total;
    out_stats->cpu_ticks_window = ts.cpu_ticks_window;
    out_stats->cpu_throttle_events = ts.cpu_throttle_events;

    out_stats->io_read_bytes_total = ts.io_read_bytes_total;
    out_stats->io_read_bytes_window = ts.io_read_bytes_window;
    out_stats->io_write_bytes_total = ts.io_write_bytes_total;
    out_stats->io_write_bytes_window = ts.io_write_bytes_window;
    out_stats->io_throttle_events = ts.io_throttle_events;

    out_stats->mem_bytes_current = ts.mem_bytes_current;
    out_stats->mem_bytes_peak = ts.mem_bytes_peak;
    out_stats->mem_throttle_events = ts.mem_throttle_events;

    out_stats->io_ops_total = ts.io_ops_total;
    out_stats->io_ops_interactive = ts.io_ops_interactive;
    out_stats->io_ops_latency = ts.io_ops_latency;
    out_stats->io_ops_throughput = ts.io_ops_throughput;
    out_stats->io_ops_background = ts.io_ops_background;

    out_stats->sched_context_switches = ss.context_switches;
    out_stats->sched_yields = ss.yield_calls;
    out_stats->sched_preempt_switches = ss.preempt_switches;
    out_stats->sched_runs_interactive = ss.intent_runs_interactive;
    out_stats->sched_runs_latency = ss.intent_runs_latency;
    out_stats->sched_runs_throughput = ss.intent_runs_throughput;
    out_stats->sched_runs_background = ss.intent_runs_background;

    return KS_STATUS_OK;
}

ks_status_t cka_sched_set_current_thread_intent(uint32_t intent)
{
    thread_t *cur = sched_current();
    if (!cur) {
        return KS_STATUS_INVALID_STATE;
    }
    sched_set_thread_intent(cur, intent);
    return KS_STATUS_OK;
}

// ---- Capability operations ----

ks_status_t cka_cap_create(cap_type_t type, cap_rights_t rights, void *obj, cap_id_t *out_id)
{
    if (!g_core_caps || !out_id) {
        return KS_STATUS_INVALID_ARG;
    }
    return cap_create(g_core_caps, type, rights, obj, out_id);
}

ks_status_t cka_cap_retain(cap_id_t id)
{
    if (!g_core_caps) {
        return KS_STATUS_INVALID_STATE;
    }
    // Capabilities are table entries and are not reference-counted. "Retain"
    // is treated as a validity check so callers can safely gate later use.
    return cap_table_lookup(g_core_caps, (cap_handle_t)id, 0) ? KS_STATUS_OK
                                                          : KS_STATUS_NOT_FOUND;
}

ks_status_t cka_cap_release(cap_id_t id)
{
    if (!g_core_caps) {
        return KS_STATUS_INVALID_STATE;
    }
    cap_status_t st = cap_drop(g_core_caps, (cap_handle_t)id);
    switch (st) {
        case CAP_OK:             return KS_STATUS_OK;
        case CAP_ERR_INVALID:    return KS_STATUS_NOT_FOUND;
        case CAP_ERR_DENIED:     return KS_STATUS_NO_RIGHTS;
        case CAP_ERR_NO_SPACE:   return KS_STATUS_OUT_OF_MEMORY;
        default:                 return KS_STATUS_INTERNAL;
    }
}

// ---- IPC ----

ks_status_t cka_ipc_send(cap_id_t endpoint_cap, const ks_ipc_msg_t *msg)
{
    if (!g_core_caps || !msg) {
        return KS_STATUS_INVALID_ARG;
    }
    return ks_from_ipc_status(ipc_send_cap(g_core_caps, (cap_handle_t)endpoint_cap, msg));
}

ks_status_t cka_ipc_recv(cap_id_t endpoint_cap, ks_ipc_msg_t *msg)
{
    if (!g_core_caps || !msg) {
        return KS_STATUS_INVALID_ARG;
    }
    return ks_from_ipc_status(ipc_recv_cap(g_core_caps, (cap_handle_t)endpoint_cap, msg));
}

ks_status_t cka_ipc_try_recv(cap_id_t endpoint_cap, ks_ipc_msg_t *msg)
{
    if (!g_core_caps || !msg) {
        return KS_STATUS_INVALID_ARG;
    }
    return ks_from_ipc_status(ipc_try_recv_cap(g_core_caps, (cap_handle_t)endpoint_cap, msg));
}

ks_status_t cka_dtb_dump_devices(void)
{
    if (dtb_get_totalsize() == 0) {
        return KS_STATUS_NOT_SUPPORTED;
    }
    dtb_dump_devices();
    return KS_STATUS_OK;
}
