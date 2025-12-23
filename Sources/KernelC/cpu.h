//
//  cpu.h
//  Capaz
//
//  Created by Cosas on 12/22/25.
//

// cpu.h
#pragma once

static inline void cpu_wfe(void) {
  __asm__ volatile("wfe" ::: "memory");
}
