/*
 * kmain.c
 *
 * Kernel entry point. At this stage we are in EL1 with the MMU enabled and a
 * high-half stack established. The boot stage passes a boot_info_t pointer in x0.
 */

#include <stdint.h>

#include "boot_info.h"
#include "dtb.h"
#include "mmu.h"
#include "uart_pl011.h"

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

    if (regs) {
        uart_puts("x0     ="); uart_puthex64(regs[0]);  uart_putnl();
        uart_puts("x1     ="); uart_puthex64(regs[1]);  uart_putnl();
        uart_puts("x2     ="); uart_puthex64(regs[2]);  uart_putnl();
        uart_puts("x3     ="); uart_puthex64(regs[3]);  uart_putnl();
        uart_puts("x29(fp)="); uart_puthex64(regs[29]); uart_putnl();
        uart_puts("x30(lr)="); uart_puthex64(regs[30]); uart_putnl();
    }

    for (;;) {
        __asm__ volatile("wfi");
    }
}

void kmain(const boot_info_t *boot_info)
{
    /* Ensure we have a working UART even before DTB parsing. */
    uart_init(0);

    uart_puts("Kernel: 0.0.3\n");

    if (boot_info) {
        uart_puts("boot_info: kernel_pa="); uart_puthex64(boot_info->kernel_phys_base);
        uart_puts(" size="); uart_puthex64(boot_info->kernel_size);
        uart_puts(" entry_off="); uart_puthex64(boot_info->kernel_entry_offset);
        uart_putnl();

        uart_puts("boot_info: dtb_va="); uart_puthex64(boot_info->dtb_ptr);
        uart_puts(" dtb_size="); uart_puthex64(boot_info->dtb_size);
        uart_putnl();
    }

    /* DTB bring-up: validate and print what we can. */
    if (boot_info && boot_info->dtb_ptr != 0) {
        if (dtb_init((const void *)(uintptr_t)boot_info->dtb_ptr, boot_info->dtb_size)) {
            dtb_dump_summary();

            /* If DTB gives us a UART base, switch to it (fallback otherwise). */
            uint64_t uart_phys = 0;
            if (dtb_find_pl011_uart(&uart_phys)) {
                uart_puts("UART: switching to DTB base "); uart_puthex64(uart_phys); uart_putnl();
                uart_init(uart_phys);
            }
        } else {
            uart_puts("DTB: invalid header (fallback to hardcoded UART)\n");
        }
    } else {
        uart_puts("DTB: no pointer provided (fallback to hardcoded UART)\n");
    }

    /* Install kernel page tables (TTBR1) and disable TTBR0. */
    mmu_init(boot_info);

    uart_puts("Kernel: MMU initialized\n");

    for (;;) {
        __asm__ volatile ("wfi");
    }
}
