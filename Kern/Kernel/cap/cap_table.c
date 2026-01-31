// cap_table.c
// Capability table storage + handle validation.

#include "cap_table.h"

#include "cap_entry.h"
#include <stddef.h>

// --- Internal helpers -------------------------------------------------------

static inline bool cap_slot_valid_index(uint32_t idx) {
    return idx < (uint32_t)CONFIG_CAP_TABLE_SLOTS;
}

static cap_status_t cap_table_alloc_slot(cap_table_t *t, uint32_t *out_idx) {
    if (!t || !out_idx) {
        return CAP_ERR_INVALID;
    }
    if (t->free_top == 0) {
        return CAP_ERR_NO_SLOTS;
    }

    // Pop a free index.
    uint32_t idx = t->free_stack[--t->free_top];
    if (!cap_slot_valid_index(idx)) {
        // Table corruption; fail closed.
        return CAP_ERR_INVALID;
    }
    *out_idx = idx;
    return CAP_OK;
}

static void cap_table_free_slot(cap_table_t *t, uint32_t idx) {
    if (!t || !cap_slot_valid_index(idx)) {
        return;
    }
    if (t->free_top < (uint32_t)CONFIG_CAP_TABLE_SLOTS) {
        t->free_stack[t->free_top++] = idx;
    }
}

static cap_entry_t *cap_lookup_ex(cap_table_t *t,
                                  cap_handle_t h,
                                  cap_rights_t need,
                                  cap_status_t *status_out) {
    if (!t) {
        if (status_out) {
            *status_out = CAP_ERR_INVALID;
        }
        return NULL;
    }

    uint32_t idx = cap_handle_index(h);
    if (!cap_slot_valid_index(idx)) {
        if (status_out) {
            *status_out = CAP_ERR_INVALID;
        }
        return NULL;
    }

    cap_entry_t *e = t->slots[idx];
    if (!e || (e->flags & CAP_ENTRY_FLAG_VALID) == 0) {
        if (status_out) {
            *status_out = CAP_ERR_NO_ENTRY;
        }
        return NULL;
    }

    uint32_t gen = cap_handle_gen(h);
    if (gen == 0 || gen != t->gens[idx] || gen != e->gen) {
        if (status_out) {
            *status_out = CAP_ERR_INVALID;
        }
        return NULL;
    }

    if ((e->rights & need) != need) {
        if (status_out) {
            *status_out = CAP_ERR_DENIED;
        }
        return NULL;
    }

    if (status_out) {
        *status_out = CAP_OK;
    }
    return e;
}

static inline uint32_t cap_bump_gen(uint32_t gen) {
    // Never allow generation 0 (reserved as "invalid").
    gen += 1u;
    if (gen == 0) {
        gen = 1u;
    }
    return gen;
}

// --- Public API -------------------------------------------------------------

void cap_table_init(cap_table_t *t) {
    if (!t) {
        return;
    }

    for (uint32_t i = 0; i < (uint32_t)CONFIG_CAP_TABLE_SLOTS; i++) {
        t->slots[i] = NULL;
        t->gens[i] = 1u;
        t->free_stack[i] = i;
    }
    t->free_top = (uint32_t)CONFIG_CAP_TABLE_SLOTS;
}

cap_status_t cap_table_insert(cap_table_t *t,
                              cap_type_t type,
                              cap_rights_t rights,
                              void *obj,
                              cap_handle_t *out) {
    if (!t || !out) {
        return CAP_ERR_INVALID;
    }

    uint32_t idx = 0;
    cap_status_t st = cap_table_alloc_slot(t, &idx);
    if (st != CAP_OK) {
        return st;
    }

    cap_entry_t *e = cap_entry_alloc();
    if (!e) {
        cap_table_free_slot(t, idx);
        return CAP_ERR_NO_MEM;
    }

    e->type = type;
    e->rights = rights;
    e->obj = obj;
    e->gen = t->gens[idx];
    e->flags = CAP_ENTRY_FLAG_VALID;

    t->slots[idx] = e;
    *out = cap_handle_make(e->gen, idx);
    return CAP_OK;
}

cap_entry_t *cap_lookup(cap_table_t *t, cap_handle_t h, cap_rights_t need) {
    return cap_lookup_ex(t, h, need, NULL);
}

cap_status_t cap_table_remove(cap_table_t *t, cap_handle_t h) {
    if (!t) {
        return CAP_ERR_INVALID;
    }

    cap_status_t st = CAP_OK;
    cap_entry_t *e = cap_lookup_ex(t, h, 0, &st);
    if (!e) {
        return st;
    }

    uint32_t idx = cap_handle_index(h);
    if (!cap_slot_valid_index(idx)) {
        return CAP_ERR_INVALID;
    }

    t->slots[idx] = NULL;
    cap_entry_free(e);

    t->gens[idx] = cap_bump_gen(t->gens[idx]);
    cap_table_free_slot(t, idx);
    return CAP_OK;
}

void cap_table_invalidate_object(cap_table_t *t, void *obj) {
    if (!t || !obj) {
        return;
    }

    // Linear scan is fine for now (small fixed table). If needed, replace with
    // per-object reverse-mapping or a hashed index later.
    for (uint32_t i = 0; i < (uint32_t)CONFIG_CAP_TABLE_SLOTS; i++) {
        cap_entry_t *e = t->slots[i];
        if (!e) {
            continue;
        }
        if ((e->flags & CAP_ENTRY_FLAG_VALID) == 0) {
            continue;
        }
        if (e->obj != obj) {
            continue;
        }

        t->slots[i] = NULL;
        cap_entry_free(e);
        t->gens[i] = cap_bump_gen(t->gens[i]);
        cap_table_free_slot(t, i);
    }
}

cap_status_t cap_table_invalidate(cap_table_t *t, cap_handle_t h)
{
    // Invalidate a specific handle by removing its entry.
    // cap_table_remove bumps the generation so existing handles become stale.
    return cap_table_remove(t, h);
}
