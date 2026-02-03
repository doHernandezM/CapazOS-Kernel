#pragma once

#include <stdint.h>

// Console/UART IPC contract tags.
// Tags are intentionally stable and shared between Core and Kernel.
enum {
    CONSOLE_TAG_WRITE = 1,
    CONSOLE_TAG_RX_EVENT = 2,
    CONSOLE_TAG_QUERY_INTENTS = 3,   // reserved
    CONSOLE_TAG_OPEN_CONTRACT = 4    // reserved
};

// Message layouts:
// - CONSOLE_TAG_WRITE: raw bytes in ks_ipc_msg_t.data, len = byte count.
