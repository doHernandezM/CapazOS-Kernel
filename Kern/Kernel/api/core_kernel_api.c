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

static cap_table_t *g_core_caps;
static cap_handle_t g_kernel_log_ep = CAP_HANDLE_INVALID;

typedef struct ks_contract_entry {
    uint32_t rights;
    uint32_t policy;
    uint8_t in_use;
} ks_contract_entry_t;

static ks_contract_entry_t g_contracts[64];

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
            return KS_STATUS_OUT_OF_MEMORY;
        }
        if (!hal_block_read(lba + offset_sectors, tmp, chunk)) {
            kheap_free_pages(tmp, (uint32_t)pages);
            return KS_STATUS_INTERNAL;
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
    return KS_STATUS_OK;
}

ks_status_t cka_block_write(uint64_t lba, uint32_t count, const void *buf, size_t buf_len)
{
    return cka_block_write_intent(lba, count, buf, buf_len, KS_IO_INTENT_LATENCY);
}

ks_status_t cka_block_write_intent(uint64_t lba, uint32_t count, const void *buf, size_t buf_len, uint32_t intent)
{
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
            return KS_STATUS_OUT_OF_MEMORY;
        }
        memcpy(tmp, src + ((uint64_t)offset_sectors * (uint64_t)sector_size), (size_t)chunk_bytes);
        if (!hal_block_write(lba + offset_sectors, tmp, chunk)) {
            kheap_free_pages(tmp, (uint32_t)pages);
            return KS_STATUS_INTERNAL;
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
    return KS_STATUS_OK;
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
    return kmalloc(size);
}

void cka_kfree(void *ptr)
{
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
