#pragma once

#include <stdint.h>

#include "ks_types.h"

/// Boot-time information shared from the kernel to the Core component.
///
/// The Core side treats all pointer fields as opaque.
typedef struct core_boot_if {
    /// ABI / layout version of this struct.
    uint32_t version;

    /// Kernel pointer to the Core task's capability table (opaque to Core).
    void *core_caps;

    /// Kernel pointer to the Core task object (opaque to Core).
    void *core_task;

    /// Capability handle for the console endpoint, seeded into the Core task.
    ks_cap_handle_t console_ep;

    /// UART driver command endpoint (write / ioctl).
    ks_cap_handle_t uart_cmd_ep;

    /// UART driver event endpoint (RX events).
    ks_cap_handle_t uart_evt_ep;

    /// Kernel log endpoint (recv-only), seeded into Core.
    ks_cap_handle_t kernel_log_ep;
} core_boot_if_t;

/// Called by the kernel exactly once before invoking `core_main()`.
/// Stores a pointer to the boot interface for later access by Core.
void core_boot_attach(const core_boot_if_t *boot_if);

/// Returns the attached boot interface pointer, or NULL if not attached.
const core_boot_if_t *core_boot_if(void);

/// Entrypoint for the Core component.
///
/// The kernel owns the boot process. The only contract is that `core_main()` is invoked exactly once
/// after the kernel has completed early init and established the Core execution context.
int32_t core_main(void);
