/*
 * kcrt0.c
 *
 * Freestanding C runtime entry for the Capaz kernel.
 *
 * The boot stage (Sources/Arch/aarch64/start.S) enables the MMU and then
 * branches into this file at symbol _kcrt0. x0 contains a pointer to a
 * boot_info_t structure (in high-half direct-mapped VA space).
 */

#include <stdint.h>

#include "boot_info.h"

/* Prototype for the kernel’s main entry point defined in kmain.c. */
void kmain(const boot_info_t *boot_info);

/*
 * Real C entry point once a high-half stack is established.
 *
 * IMPORTANT:
 *  - Must NOT be static, because _kcrt0's inline asm branches to it by symbol.
 *  - Mark noreturn to match control flow.
 */
__attribute__((noreturn, used))
void _kcrt0_c(const boot_info_t *boot_info);

/*
 * _kcrt0 – kernel entry trampoline
 *
 * Requirements for early boot:
 *  - x0 may be a non-canonical 48-bit pointer (top 16 bits not sign-extended).
 *  - SP starts on the low identity-mapped boot stack in RAM (0x4000_0000+...).
 *  - The kernel will soon disable TTBR0 (TCR.EPD0=1); any further use of the
 *    low stack would immediately fault.
 *
 * This trampoline (naked, no prologue) canonicalizes x0 and moves SP into the
 * high-half direct map before tail-calling the real C entry.
 *
 * IMPORTANT: In a naked function, the body must be *only* inline asm.
 */
__attribute__((naked, section(".text._kcrt0"), used))
void _kcrt0(void) {
    __asm__ volatile(
        /* Canonicalize x0 for 48-bit VA: sign-extend bit 47 into the top 16 bits. */
        "lsl    x0, x0, #16\n"
        "asr    x0, x0, #16\n"

        /*
         * Move SP from low RAM alias (0x4000_0000+off) to high-half alias:
         *   sp = HH_PHYS_4000_BASE + (sp - 0x4000_0000)
         *
         * This matches start.S which maps PA 0x4000_0000.. as
         * VA 0xFFFF8000_4000_0000.. (direct map base for that PA window).
         */
        "mov    x1, sp\n"
        "movz   x2, #0x4000, lsl #16\n"         /* x2 = 0x4000_0000 */
        "sub    x1, x1, x2\n"
        "ldr    x2, =0xFFFF800040000000\n"      /* HH_PHYS_4000_BASE */
        "add    x1, x1, x2\n"
        "mov    sp, x1\n"

        /* Tail-call into the real C entry. */
        "b      _kcrt0_c\n"
    );
}

void _kcrt0_c(const boot_info_t *boot_info) {
    kmain(boot_info);

    /* Should not return. Enter low-power wait if it does. */
    for (;;) {
        __asm__ volatile("wfe");
    }
}
