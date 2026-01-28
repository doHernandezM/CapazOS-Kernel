// console_service_v1.h
// Console/Serial Service ABI v1 (draft)
//
// NOTE: Phase 0 defines tags and mode flags only; Phase 2 will implement
// IPC message handling for these tags.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAPAZ_CONSOLE_SERVICE_ABI_V1 1u

// IPC message tags (v1). Values are stable once published.
typedef enum console_msg_tag_v1 : uint32_t {
    CONSOLE_MSG_NOP          = 0,
    CONSOLE_MSG_WRITE        = 1,
    CONSOLE_MSG_READ         = 2,
    CONSOLE_MSG_SETMODE      = 3,
    CONSOLE_MSG_SUBSCRIBE    = 4,
    CONSOLE_MSG_READ_EVENT   = 5,

    // Responses / notifications (reserved for Phase 2+)
    CONSOLE_MSG_READ_RSP     = 100,
    CONSOLE_MSG_STATUS_RSP   = 101,
    CONSOLE_MSG_EVENT        = 102,
} console_msg_tag_t;

// Console mode flags (v1).
typedef enum console_mode_flags_v1 : uint32_t {
    CONSOLE_MODE_NONE        = 0u,

    // Enable VT100/ANSI escape processing in the console server.
    // v1: kernel is pass-through; server may interpret in future.
    CONSOLE_MODE_ANSI        = 1u << 0,

    // Raw input mode (no line editing).
    CONSOLE_MODE_RAW         = 1u << 1,

    // Cooked input mode (line discipline). Reserved for Core console server.
    CONSOLE_MODE_COOKED      = 1u << 2,
} console_mode_flags_t;

#ifdef __cplusplus
} // extern "C"
#endif
