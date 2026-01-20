// Capability operations (kernel-internal)
//
// Milestone M7 Phase 3: first explicit cap operations.
// These primitives are built atop cap_table_t and are NOT exposed to Core directly.

#include "cap_ops.h"

#include "cap/cap_entry.h"
#include "debug/panic.h"

// Small helper for internal selftests.
#ifdef DEBUG
static void expect(int cond, const char *msg)
{
    if (!cond) {
        panic("cap_ops_selftest: %s", msg);
    }
}
#endif

cap_status_t cap_create(cap_table_t *t,
                        cap_type_t type,
                        cap_rights_t rights,
                        void *obj,
                        cap_handle_t *out)
{
    if (!t || !out) {
        return CAP_ERR_INVALID;
    }
    return cap_table_insert(t, type, rights, obj, out);
}

cap_status_t cap_dup(cap_table_t *src,
                     cap_handle_t h,
                     cap_table_t *dst,
                     cap_rights_t mask,
                     cap_handle_t *out)
{
    if (!src || !dst || !out) {
        return CAP_ERR_INVALID;
    }

    // Requires explicit DUP right.
    cap_entry_t *e = cap_table_lookup(src, h, CAP_R_DUP);
    if (!e) {
        return CAP_ERR_DENIED;
    }

    cap_rights_t new_rights = (e->rights & mask);
    return cap_table_insert(dst, e->type, new_rights, e->obj, out);
}

cap_status_t cap_transfer(cap_table_t *src,
                          cap_handle_t h,
                          cap_table_t *dst,
                          cap_rights_t mask,
                          cap_handle_t *out)
{
    if (!src || !dst || !out) {
        return CAP_ERR_INVALID;
    }

    // For M7: implement transfer as dup + drop, with rollback if src removal fails.
    cap_entry_t *e = cap_table_lookup(src, h, CAP_R_TRANSFER);
    if (!e) {
        return CAP_ERR_DENIED;
    }

    cap_status_t st = cap_table_insert(dst, e->type, (e->rights & mask), e->obj, out);
    if (st != CAP_OK) {
        return st;
    }

    st = cap_table_remove(src, h);
    if (st != CAP_OK) {
        (void)cap_table_remove(dst, *out);
        return st;
    }

    return CAP_OK;
}

cap_status_t cap_drop(cap_table_t *t, cap_handle_t h)
{
    if (!t) {
        return CAP_ERR_INVALID;
    }
    return cap_table_remove(t, h);
}

cap_status_t cap_invalidate(cap_table_t *t, cap_handle_t h)
{
    if (!t) {
        return CAP_ERR_INVALID;
    }
    return cap_table_invalidate(t, h);
}

void cap_ops_selftest(cap_table_t *t)
{
    (void)t;

#ifdef DEBUG
    if (!t) {
        panic("cap_ops_selftest: null table");
    }

    // Minimal invariants: create -> lookup -> drop; stale handle must fail.
    cap_handle_t h = 0;
    cap_status_t st = cap_create(t, CAP_TYPE_SERVICE, (CAP_R_READ | CAP_R_DUP | CAP_R_TRANSFER), (void *)0x1234, &h);
    expect(st == CAP_OK, "cap_create failed");

    cap_entry_t *e = cap_table_lookup(t, h, CAP_R_READ);
    expect(e != NULL, "cap_lookup failed");
    expect(e->type == CAP_TYPE_SERVICE, "type mismatch");
    expect(e->obj == (void *)0x1234, "obj mismatch");

    st = cap_drop(t, h);
    expect(st == CAP_OK, "cap_drop failed");

    // Stale handle must fail due to gen bump.
    e = cap_table_lookup(t, h, CAP_R_READ);
    expect(e == NULL, "stale handle should fail");

    // Dup/transfer smoke.
    cap_handle_t h1 = 0;
    st = cap_create(t, CAP_TYPE_TIMER_TOKEN, (CAP_R_READ | CAP_R_DUP | CAP_R_TRANSFER), (void *)0xBEEF, &h1);
    expect(st == CAP_OK, "cap_create #2 failed");

    cap_handle_t h_dup = 0;
    st = cap_dup(t, h1, t, CAP_R_READ, &h_dup);
    expect(st == CAP_OK, "cap_dup failed");

    cap_handle_t h_xfer = 0;
    st = cap_transfer(t, h1, t, CAP_R_READ, &h_xfer);
    expect(st == CAP_OK, "cap_transfer failed");

    (void)cap_drop(t, h_dup);
    (void)cap_drop(t, h_xfer);
#endif
}
