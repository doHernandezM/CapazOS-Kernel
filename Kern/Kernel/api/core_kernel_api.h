#pragma once

// Internal Core<->Kernel API surface.
//
// A0/A1 boundary rule: Kernel provides mechanisms; Core provides policy.
// This header is an internal API (not a versioned ABI) and may evolve with
// the repo as long as Kernel and Core are updated together.

#include <stddef.h>
#include <stdint.h>

#include "cap/cap_table.h"
#include "cap/cap_rights.h"
#include "cap/cap_types.h"
#include "ipc/endpoint.h"
#include "ks_status.h"
#include "sched/intent.h"

// Internal capability identifier type used across the Core<->Kernel API.
// The kernel's canonical handle type is cap_handle_t.
typedef cap_handle_t cap_id_t;

#ifdef __cplusplus
extern "C" {
#endif

// ---- Bring-up / attachment ----

// Called by Kernel during bootstrap to identify the Core task's capability
// space. Core-callable wrappers that operate on capability IDs use this table.
void cka_attach_core_caps(cap_table_t *core_caps);

// Returns the attached Core cap table (may be NULL if not attached yet).
cap_table_t *cka_core_caps(void);

// ---- Logging ----

void cka_early_log(const char *s);
void cka_console_write(const char *s, size_t len);

// Convenience helper for logging a NUL-terminated string.
// This is intentionally tiny and can be used from Core shims.
void cka_log_write(const char *s);

// ---- Memory ----

void *cka_kmalloc(size_t size);
void cka_kfree(void *ptr);

// Aligned allocation helpers for Core.
// These are intentionally minimal and exist primarily for Swift runtime shims.
void *cka_malloc(size_t size, size_t alignment);
void cka_free(void *ptr);

void *cka_memcpy(void *dst, const void *src, size_t n);
void *cka_memset(void *dst, int c, size_t n);

// ---- Build Info ----

typedef struct ks_build_info {
    uint64_t kernel_build_number;
    const char *build_date;
    const char *build_version;
    const char *build_environment;
    const char *kernel_version;
    const char *kernel_machine;
    const char *core_name;
    const char *core_version;
} ks_build_info_t;

ks_status_t cka_get_build_info(ks_build_info_t *out_info);

// ---- Block device ----

typedef struct ks_block_info {
    uint32_t sector_size;
    uint32_t _pad0;
    uint64_t capacity_sectors;
} ks_block_info_t;

typedef enum ks_io_intent {
    KS_IO_INTENT_LATENCY = 1,
    KS_IO_INTENT_THROUGHPUT = 2,
    KS_IO_INTENT_BACKGROUND = 3,
} ks_io_intent_t;

ks_status_t cka_block_get_info(ks_block_info_t *out_info);
ks_status_t cka_block_read(uint64_t lba, uint32_t count, void *buf, size_t buf_len);
ks_status_t cka_block_read_intent(uint64_t lba, uint32_t count, void *buf, size_t buf_len, uint32_t intent);
ks_status_t cka_block_write(uint64_t lba, uint32_t count, const void *buf, size_t buf_len);
ks_status_t cka_block_write_intent(uint64_t lba, uint32_t count, const void *buf, size_t buf_len, uint32_t intent);

// ---- Contracts ----

typedef enum ks_contract_kind {
    KS_CONTRACT_FAT32 = 1,
} ks_contract_kind_t;

ks_status_t cka_contract_open(uint32_t kind, uint32_t rights, uint32_t policy, uint64_t *out_handle);
ks_status_t cka_contract_get(uint64_t handle, uint32_t *out_rights, uint32_t *out_policy);

// ---- Scheduling ----

void cka_yield(void);

// Placeholder sleep hook. Until the kernel exposes a timer-based sleep,
// this yields in a loop.
void cka_sleep_ticks(uint64_t ticks);

typedef enum ks_sched_intent {
    KS_SCHED_INTENT_INTERACTIVE = SCHED_INTENT_INTERACTIVE,
    KS_SCHED_INTENT_LATENCY = SCHED_INTENT_LATENCY,
    KS_SCHED_INTENT_THROUGHPUT = SCHED_INTENT_THROUGHPUT,
    KS_SCHED_INTENT_BACKGROUND = SCHED_INTENT_BACKGROUND,
} ks_sched_intent_t;

typedef struct ks_sched_contract {
    uint32_t intent;
    uint32_t flags;
    uint64_t window_ticks;
    uint64_t cpu_ticks_limit;
    uint64_t io_read_bytes_limit;
    uint64_t io_write_bytes_limit;
    uint64_t mem_bytes_limit;
} ks_sched_contract_t;

typedef struct ks_sched_stats {
    uint64_t tick_now;
    uint64_t window_start_tick;
    uint64_t window_ticks;

    uint64_t cpu_ticks_total;
    uint64_t cpu_ticks_window;
    uint64_t cpu_throttle_events;

    uint64_t io_read_bytes_total;
    uint64_t io_read_bytes_window;
    uint64_t io_write_bytes_total;
    uint64_t io_write_bytes_window;
    uint64_t io_throttle_events;

    uint64_t mem_bytes_current;
    uint64_t mem_bytes_peak;
    uint64_t mem_throttle_events;

    uint64_t io_ops_total;
    uint64_t io_ops_interactive;
    uint64_t io_ops_latency;
    uint64_t io_ops_throughput;
    uint64_t io_ops_background;

    uint64_t sched_context_switches;
    uint64_t sched_yields;
    uint64_t sched_preempt_switches;
    uint64_t sched_runs_interactive;
    uint64_t sched_runs_latency;
    uint64_t sched_runs_throughput;
    uint64_t sched_runs_background;
} ks_sched_stats_t;

ks_status_t cka_sched_set_contract(const ks_sched_contract_t *contract);
ks_status_t cka_sched_get_contract(ks_sched_contract_t *out_contract);
ks_status_t cka_sched_get_stats(ks_sched_stats_t *out_stats);
ks_status_t cka_sched_set_current_thread_intent(uint32_t intent);

// ---- Capability operations (Core-callable) ----

// Create and insert a capability into the Core task's cap table.
ks_status_t cka_cap_create(cap_type_t type, cap_rights_t rights, void *obj, cap_id_t *out_id);

ks_status_t cka_cap_retain(cap_id_t id);
ks_status_t cka_cap_release(cap_id_t id);

// ---- IPC (Core-callable) ----

ks_status_t cka_ipc_send(cap_id_t endpoint_cap, const ks_ipc_msg_t *msg);
ks_status_t cka_ipc_recv(cap_id_t endpoint_cap, ks_ipc_msg_t *msg);
ks_status_t cka_ipc_try_recv(cap_id_t endpoint_cap, ks_ipc_msg_t *msg);

// Kernel-only hook: attach the kernel log endpoint for forwarding.
void cka_attach_kernel_log_ep(cap_id_t endpoint_cap);

// DTB device enumeration (prints to KernelLog/console).
ks_status_t cka_dtb_dump_devices(void);

#ifdef __cplusplus
} // extern "C"
#endif
