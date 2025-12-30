/*
 * kcrt0.c
 *
 * Freestanding C runtime entry for the Capaz kernel.  This function
 * is placed at the beginning of the kernel image and is the first
 * code executed once the boot stage has enabled the MMU and
 * branched to the loaded kernel image.  It receives a pointer to a
 * boot_info structure in x0 and forwards that pointer to kmain().
 *
 * In a more complete kernel this function would also clear the
 * kernel’s .bss section, relocate .data if necessary, and call
 * static constructors.  Those responsibilities are deferred to
 * later milestones.
 */

#include <stdint.h>

/* Prototype for the kernel’s main entry point defined in kmain.c. */
void kmain(void *boot_info);

/*
 * _kcrt0 – kernel C runtime entry
 *
 * The boot stage branches to this symbol at offset zero in the
 * kernel image.  It simply calls kmain() with the boot_info pointer
 * and never returns.  If kmain() ever returns, we park the CPU in an
 * infinite wfi loop.
 */
__attribute__((section(".text._kcrt0"), used))
void _kcrt0(void *boot_info) {
    kmain(boot_info);
    /* Should not return.  Enter low‑power wait if it does. */
    for (;;) {
        __asm__ volatile ("wfi");
    }
}