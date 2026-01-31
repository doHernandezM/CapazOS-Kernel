//
//  cap_status_ks.h
//  C Kernel
//
//  Created by Cosas on 1/19/26.
//

#pragma once

#include <stdint.h>

#include "cap_table.h"
#include "core_kernel_abi_v2.h"

// One canonical mapping from internal cap_status_t -> KS ABI v2 codes.
// All ABI layers should call this instead of duplicating switch statements.
ks_cap_status_t cap_status_to_ks_status(cap_status_t st);
