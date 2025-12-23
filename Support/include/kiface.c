//
//  kiface.c
//  OSpost
//
//  Phase 0: stub implementations
//

#include "kiface.h"

kstatus_t k_submit_work(const work_request_t* req, cap_handle_t subject) {
    (void)req;
    (void)subject;
    // Phase 0: no scheduler yet; API exists so later phases don’t retrofit it.
    return K_OK;
}
