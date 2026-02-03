#pragma once

#include <stdint.h>

// KernelLog IPC contract tags.
enum {
    KLOG_TAG_WRITE = 1
};

// Message layout:
// - KLOG_TAG_WRITE: raw bytes in ks_ipc_msg_t.data, len = byte count.
