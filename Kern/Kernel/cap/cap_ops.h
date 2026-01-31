// Capability operations (kernel-internal)
//
// These are explicit, auditable primitives built atop cap_table_t.
// A Core-facing ABI for capability operations may be added in the future, but the
// kernel must first have a correct internal substrate.

#pragma once

#include <stdint.h>

#include "cap_table.h"

#ifdef __cplusplus
extern "C" {
#endif

// Allocate a new capability entry and install it in the table.
cap_status_t cap_create(cap_table_t *t,
                        cap_type_t type,
                        cap_rights_t rights,
                        void *obj,
                        cap_handle_t *out);

// Duplicate a capability from src to dst.
// Requires CAP_R_DUP in the source entry.
// Rights in the destination are: (src_rights & mask).
cap_status_t cap_dup(cap_table_t *src,
                     cap_handle_t h,
                     cap_table_t *dst,
                     cap_rights_t mask,
                     cap_handle_t *out);

// Transfer a capability from src to dst.
// Requires CAP_R_TRANSFER in the source entry.
// Transfer is implemented as (dup + drop) for now.
cap_status_t cap_transfer(cap_table_t *src,
                          cap_handle_t h,
                          cap_table_t *dst,
                          cap_rights_t mask,
                          cap_handle_t *out);

// Drop a capability (remove entry, bump generation, free entry object).
cap_status_t cap_drop(cap_table_t *t, cap_handle_t h);

// Revoke / invalidate stub.
// For now this is equivalent to drop, but named explicitly for future
// revocation work (e.g. invalidating derived copies).
cap_status_t cap_invalidate(cap_table_t *t, cap_handle_t h);

// Debug-only self test for basic correctness. No output required.
// Safe to call in thread context after allocators are initialized.
void cap_ops_selftest(cap_table_t *t);

#ifdef __cplusplus
}
#endif
