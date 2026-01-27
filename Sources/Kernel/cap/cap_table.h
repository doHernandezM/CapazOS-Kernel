// Kernel-private capability table (cap-space) implementation.
//
// This is not exposed to Core; Core will eventually hold opaque cap_handle_t values.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "cap/cap_rights.h"
#include "cap/cap_types.h"

// Stable opaque handle type.
// Packing (v1): [gen:32][index:32]
typedef uint64_t cap_handle_t;
#define CAP_HANDLE_T_DEFINED 1

static inline uint32_t cap_handle_index(cap_handle_t h) { return (uint32_t)(h & 0xFFFFFFFFu); }
static inline uint32_t cap_handle_gen(cap_handle_t h) { return (uint32_t)(h >> 32); }
static inline cap_handle_t cap_handle_make(uint32_t gen, uint32_t idx) { return ((uint64_t)gen << 32) | (uint64_t)idx; }

#ifndef CONFIG_CAP_TABLE_SLOTS
#define CONFIG_CAP_TABLE_SLOTS 256
#endif

typedef enum cap_status {
    CAP_OK = 0,
    CAP_ERR_INVALID = -1,
    CAP_ERR_NO_SLOTS = -2,
    CAP_ERR_DENIED = -3,
    CAP_ERR_NO_MEM = -4,
} cap_status_t;

// Compat aliases used by ABI v2 glue.
#define CAP_ERR_NO_SPACE CAP_ERR_NO_SLOTS
#define CAP_ERR_NO_RIGHTS CAP_ERR_DENIED

// Older names seen in some glue code / status mapping.
#ifndef CAP_ERR_NO_ENTRY
#define CAP_ERR_NO_ENTRY CAP_ERR_INVALID
#endif
#ifndef CAP_ERR_STALE
#define CAP_ERR_STALE CAP_ERR_INVALID
#endif
#ifndef CAP_ERR_RIGHTS
#define CAP_ERR_RIGHTS CAP_ERR_DENIED
#endif

// Backwards-compatibility alias used by some callers.
// Historically, "OOM" meant we failed to allocate a cap entry.
#ifndef CAP_ERR_OOM
#define CAP_ERR_OOM CAP_ERR_NO_MEM
#endif

typedef struct cap_entry cap_entry_t;

typedef struct cap_table {
    cap_entry_t *slots[CONFIG_CAP_TABLE_SLOTS];
    uint32_t gens[CONFIG_CAP_TABLE_SLOTS];
    uint32_t free_stack[CONFIG_CAP_TABLE_SLOTS];
    uint32_t free_top;
} cap_table_t;

void cap_table_init(cap_table_t *t);

// Create a new entry in the table (allocates a slab-backed cap_entry_t).
cap_status_t cap_table_insert(cap_table_t *t,
                              cap_type_t type,
                              cap_rights_t rights,
                              void *obj,
                              cap_handle_t *out);

// Lookup (checks generation + validity + rights).
cap_entry_t *cap_lookup(cap_table_t *t, cap_handle_t h, cap_rights_t need);

// Convenience alias used by cap_ops.
static inline cap_entry_t *cap_table_lookup(cap_table_t *t, cap_handle_t h, cap_rights_t need) {
    return cap_lookup(t, h, need);
}

// Remove (drop) an entry; bumps generation and frees the entry object.
cap_status_t cap_table_remove(cap_table_t *t, cap_handle_t h);

// currently identical to remove.
cap_status_t cap_table_invalidate(cap_table_t *t, cap_handle_t h);
