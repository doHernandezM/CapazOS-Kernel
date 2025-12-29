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

void kmain(void)
{
    uart_puts("Kernel stage reached\n");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}