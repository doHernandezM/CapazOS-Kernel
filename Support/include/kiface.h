//
//  kiface.h
//  OSpost
//
//  Phase 0: kernel-facing interface contracts
//

#pragma once
#include <stdint.h>
#include "work_request.h"
#include "capability.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t kstatus_t;
enum { K_OK = 0, K_ERR_UNIMPL = -1 };

// Future: submits work to scheduler. Phase 0: stub/no-op.
kstatus_t k_submit_work(const work_request_t* req, cap_handle_t subject);

#ifdef __cplusplus
}
#endif
