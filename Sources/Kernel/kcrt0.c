/*
 * kcrt0.c
 *
 * Freestanding C runtime entry for the Capaz kernel.  This function
 * is placed at the very start of the kernel’s text segment (see
 * kernel.ld) and is the first C code executed after the boot stage
 * enables the MMU and jumps into the kernel image.  It performs the
 * minimal runtime initialisation required before calling kmain():
 *
 *   1. Zero the kernel’s BSS (.bss and COMMON) region.  The linker
 *      script exports __bss_start and __bss_end to delimit this
 *      range.
 *   2. Optionally perform other early initialisation (none yet).
 *   3. Call kmain(), passing through the boot_info pointer supplied
 *      in x0.  This preserves the first argument register so that
 *      kmain() can access boot parameters.
 *   4. If kmain() returns, halt by waiting for interrupts.
 */

#include <stdint.h>

/* Prototype of the kernel main function. */
void kmain(void *boot_info);

/* Symbols exported by the linker script marking the BSS range. */
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

/*
 * The `_kcrt0` entry point.  Placed in its own text section so the
 * linker can put it at the start of the kernel image.  The `used`
 * attribute prevents the compiler from discarding it as unused.
 */
__attribute__((section(".text._kcrt0"), used))
void _kcrt0(void *boot_info)
{
    /* 1. Clear the BSS section. */
    for (uint8_t *p = __bss_start; p < __bss_end; ++p) {
        *p = 0;
    }

    /* 2. (reserved for future runtime init) */

    /* 3. Call the kernel main function with the boot_info pointer. */
    kmain(boot_info);

    /* 4. If kmain returns, spin forever. */
    while (1) {
        __asm__ volatile("wfi" ::: "memory");
    }
}