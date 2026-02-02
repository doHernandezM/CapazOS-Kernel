#pragma once

#include <stdint.h>

// Console/UART IPC contract tags (Phase 0).
// Tags are intentionally stable and shared between Core and Kernel.
enum {
    CONSOLE_TAG_WRITE = 1,
    CONSOLE_TAG_RX_EVENT = 2,
    CONSOLE_TAG_QUERY_INTENTS = 3,   // stub (Phase 2)
    CONSOLE_TAG_OPEN_CONTRACT = 4    // stub (Phase 3)
};

// Phase 0 message layouts:
// - CONSOLE_TAG_WRITE: raw bytes in ks_ipc_msg_t.data, len = byte count.
