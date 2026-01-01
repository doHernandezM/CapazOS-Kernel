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

/*
 * Minimal boot_info layout as produced by Sources/Arch/aarch64/start.S:
 *   [0] phys_base, [8] size, [16] entry_offset
 * Keep this local to avoid depending on project include-path setup.
 */
typedef struct boot_info {
    uint64_t phys_base;
    uint64_t size;
    uint64_t entry_offset;
} boot_info_t;

/* Prototype for the kernel’s main entry point defined in kmain.c. */
void kmain(void *boot_info);

/* Real C entry point once a high-half stack is established. */
__attribute__((noreturn)) void _kcrt0_c(boot_info_t *boot_info);

/*
 * _kcrt0 – kernel entry trampoline
 *
 * Requirements for early boot:
 *  - x0 may be a non-canonical 48-bit pointer (top 16 bits not sign-extended).
 *  - SP starts on the low identity-mapped boot stack.
 *  - We disable TTBR0 early (EPD0=1), so any further use of the low stack would
 *    immediately fault.
 *
 * This trampoline (naked, no prologue) canonicalizes x0 and moves SP into the
 * high-half direct map before tail-calling the real C entry.
 *
 * IMPORTANT: In a naked function, the body must be *only* inline asm. No C
 * statements (including (void)boot_info;) are allowed by clang.
 */
__attribute__((naked, section(".text._kcrt0"), used))
void _kcrt0(void *boot_info) {
    __asm__ volatile(
        /* x0 already holds boot_info; do not touch it except canonicalization. */

        /* Canonicalize x0 for 48-bit VA: sign-extend bit 47 into the top 16 bits. */
        "lsl    x0, x0, #16\n"
        "asr    x0, x0, #16\n"

        /* Move SP from low RAM (0x4000_0000+off) to high-half alias. */
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

void _kcrt0_c(boot_info_t *boot_info) {
    kmain(boot_info);

    /* Should not return. Enter low-power wait if it does. */
    for (;;) {
        __asm__ volatile("wfe");
    }
}
