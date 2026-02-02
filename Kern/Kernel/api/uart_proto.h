#pragma once

#include <stdint.h>

#include "intent_proto.h"

// UART driver IPC contract tags (Phase 1).
enum {
    UART_TAG_WRITE = 1,
    UART_TAG_RX_EVENT = 2,
    UART_TAG_QUERY_INTENTS = 3,   // stub (Phase 2)
    UART_TAG_OPEN_CONTRACT = 4
};

enum {
    UART_CONTRACT_CMD = 1,
    UART_CONTRACT_EVT = 2
};

// Phase 1 message layouts:
// - UART_TAG_WRITE: raw bytes in ks_ipc_msg_t.data, len = byte count.
// - UART_TAG_RX_EVENT: one or more received bytes in data, len = count.
// Phase 2 message layouts:
// - UART_TAG_QUERY_INTENTS: empty request; response is sent on uart_evt_ep:
//   [u32 count][count * ks_intent_desc_t]
// - UART_TAG_OPEN_CONTRACT: request payload [u32 kind][u32 rights]
//   response payload [i32 status][u32 _pad][u64 cap_handle]
