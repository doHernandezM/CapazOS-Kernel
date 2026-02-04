#pragma once

#include <stdint.h>

// Minimal platform timer HAL.

uint64_t hal_time_now(void);
void hal_timer_init_hz(uint32_t hz);
void hal_timer_handle_irq(void);
uint64_t hal_timer_ticks_read(void);
uint32_t hal_timer_irq(void);
