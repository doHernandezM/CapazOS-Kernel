#include "boot_hal.h"

#include <stdint.h>

// Provided by loader/start.S (physical address passed by firmware).
extern uint64_t boot_dtb_pa;

const void *boot_dtb_ptr(void)
{
    return (const void *)(uintptr_t)boot_dtb_pa;
}
