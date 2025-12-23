//
//  mmu.h
//  OSpost
//
//  Created by Cosas on 12/21/25.
//

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Stage 1 bring-up: build L1 table and enable MMU (translation only).
// Caches are intentionally NOT enabled here.
void mmu_early_enable(void);

// Optional second step once stable (turn on I/D caches).
void mmu_enable_caches(void);

#ifdef __cplusplus
}
#endif
