#pragma once

#include <stdint.h>

// UART driver IPC contract tags (Phase 1).
enum {
    UART_TAG_WRITE = 1,
    UART_TAG_RX_EVENT = 2,
    UART_TAG_QUERY_INTENTS = 3,   // stub (Phase 2)
    UART_TAG_OPEN_CONTRACT = 4    // stub (Phase 3)
};

// Phase 1 message layouts:
// - UART_TAG_WRITE: raw bytes in ks_ipc_msg_t.data, len = byte count.
// - UART_TAG_RX_EVENT: one or more received bytes in data, len = count.
