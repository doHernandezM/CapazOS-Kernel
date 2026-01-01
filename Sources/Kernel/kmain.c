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
#include "mmu.h"

/*
 * Called from the EL1 exception vectors (kernel_vectors.S).
 * Prints minimal EL1 fault state and parks the CPU.
 */
__attribute__((used))
void kernel_exception_report(uint64_t esr, uint64_t far, uint64_t elr,
                             uint64_t sp, const uint64_t *regs)
{
    uart_puts("\n*** EL1 EXCEPTION ***\n");
    uart_puts("ESR_EL1="); uart_puthex64(esr); uart_putnl();
    uart_puts("FAR_EL1="); uart_puthex64(far); uart_putnl();
    uart_puts("ELR_EL1="); uart_puthex64(elr); uart_putnl();
    uart_puts("SP_EL1 ="); uart_puthex64(sp);  uart_putnl();

    /* Print a small register subset (x0-x3, x29, x30) to keep output short. */
    if (regs) {
        uart_puts("x0     ="); uart_puthex64(regs[0]); uart_putnl();
        uart_puts("x1     ="); uart_puthex64(regs[1]); uart_putnl();
        uart_puts("x2     ="); uart_puthex64(regs[2]); uart_putnl();
        uart_puts("x3     ="); uart_puthex64(regs[3]); uart_putnl();
        uart_puts("x29(fp)="); uart_puthex64(regs[29]); uart_putnl();
        uart_puts("x30(lr)="); uart_puthex64(regs[30]); uart_putnl();
    }

    for (;;) {
        __asm__ volatile("wfi");
    }
}

/*
 * Main entry point for the kernel.  The boot stage passes a pointer
 * to a boot_info structure in x0.  The kernel currently ignores
 * this pointer but will use it in future milestones to discover
 * memory layout and other boot parameters.  The signature takes
 * a void* to avoid implicit integer promotion of the first argument.
 */
void kmain(void *boot_info)
{
    /* Bring up a fresh set of translation tables before touching any
     * dynamic memory.  This will disable TTBR0 and rebuild TTBR1
     * with only the device and RAM regions mapped.  In future
     * milestones this call will also enforce W^X permissions and
     * install user page tables.  The boot_info pointer is passed
     * through to allow the MMU subsystem to consult physical
     * memory maps when mapping beyond the initial 1 GiB range.  For
     * now it is unused but must be retained in the prototype. */
    mmu_init(boot_info);

    (void)boot_info;
    uart_puts("Kernel: 0.0.2\n");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
