#include "cap_status_ks.h"

// Single translation point from internal capability status to Kernel Services ABI v2 status.
ks_cap_status_t cap_status_to_ks_status(cap_status_t st)
{
    switch (st) {
        case CAP_OK:
            return KS_CAP_OK;

        case CAP_ERR_INVALID:
#if CAP_ERR_STALE != CAP_ERR_INVALID
        case CAP_ERR_STALE:
#endif
            // Stale handles are treated as invalid by the current ABI.
            return KS_CAP_ERR_INVALID;

        case CAP_ERR_RIGHTS:
            return KS_CAP_ERR_NO_RIGHTS;

        case CAP_ERR_NO_SLOTS:
            return KS_CAP_ERR_NO_SLOTS;

        case CAP_ERR_OOM:
            return KS_CAP_ERR_OOM;

        default:
            return KS_CAP_ERR_INVALID;
    }
}
