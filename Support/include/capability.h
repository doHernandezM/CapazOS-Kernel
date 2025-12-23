//
//  capability.h
//  OSpost
//
//  Phase 0: opaque authority handles (no raw pointers as authority)
//

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t cap_handle_t;     // opaque
enum { CAP_INVALID = 0 };

typedef enum {
  CAP_KIND_NONE = 0,
  CAP_KIND_VM_OBJECT,
  CAP_KIND_ENDPOINT,
  CAP_KIND_DEVICE,
} cap_kind_t;

// Optional rights bits for later phases (kept here so APIs don’t churn)
typedef uint32_t cap_rights_t;
enum {
  CAP_R_READ   = (1u << 0),
  CAP_R_WRITE  = (1u << 1),
  CAP_R_EXEC   = (1u << 2),
  CAP_R_MAP    = (1u << 3),
  CAP_R_IPC    = (1u << 4),
  CAP_R_ADMIN  = (1u << 31),
};

#ifdef __cplusplus
}
#endif
