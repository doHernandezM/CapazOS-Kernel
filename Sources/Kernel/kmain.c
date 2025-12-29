/*
 * kmain.c
 *
 * Stub kernel entry point for the Capaz boot‑only project.  In
 * subsequent milestones this function will be invoked by a freestanding
 * C runtime after the MMU has been enabled and basic kernel state
 * initialised.  For now it simply prints a message to the UART and
 * enters an idle loop.  This file is compiled into the kernel
 * image but is not currently reached by the boot stage.
 */

#include <stdint.h>
#include "uart_pl011.h"

/*
 * Main entry point for the kernel.  The boot stage passes a pointer
 * to a boot_info structure in x0.  The kernel currently ignores
 * this pointer but will use it in future milestones to discover
 * memory layout and other boot parameters.  The signature takes
 * a void* to avoid implicit integer promotion of the first argument.
 */
void kmain(void *boot_info)
{
    (void)boot_info;
    uart_puts("Kernel stage reached\n");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}