//
//  vm_object.h
//  OSpost
//
//  Created by Cosas on 12/22/25.
//

#pragma once
#include <stdint.h>
#include <stddef.h>
#include "capability.h"

typedef enum {
  VMOBJ_ANON = 0,
} vm_object_kind_t;

typedef struct {
  vm_object_kind_t kind;
  size_t size;
  uint32_t flags;     // reserved
} vm_object_desc_t;

// In Phase 0, you can return CAP_INVALID or a dummy cap.
// This is about freezing the interface.
cap_handle_t vm_object_create(vm_object_desc_t desc);
