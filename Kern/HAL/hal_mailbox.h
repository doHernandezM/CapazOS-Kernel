#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal platform mailbox HAL.

bool hal_mailbox_call(uint32_t channel, void *message);
