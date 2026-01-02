/*
 * kcrt0.c
 *
 * Freestanding C runtime entry for the Capaz kernel.
 * First C code executed after boot stage enables MMU and jumps to kernel.
 */

#include <stdint.h>

/*
 * boot_info layout as produced by Sources/Arch/aarch64/start.S (updated):
 *   [0]  kernel_phys_base
 *   [8]  kernel_size
 *   [16] kernel_entry_offset
 *   [24] dtb_ptr   (high-half direct-map VA)
 *   [32] dtb_size  (bytes)
 *
 * Keep local to avoid relying on project include-path setup.
 */
typedef struct boot_info {
    uint64_t kernel_phys_base;
    uint64_t kernel_size;
    uint64_t kernel_entry_offset;
    uint64_t dtb_ptr;
    uint64_t dtb_size;
} boot_info_t;

/* Kernel main entry point (existing signature in your tree). */
void kmain(void *boot_info);

/* Real C entry point once a high-half stack is established. */
__attribute__((noreturn, used))
void _kcrt0_c(const boot_info_t *boot_info);

/*
 * _kcrt0 – kernel entry trampoline
 *
 * Requirements for early boot:
 *  - x0 may be a non-canonical 48-bit pointer (top 16 bits not sign-extended).
 *  - SP starts on the low identity-mapped boot stack.
 *  - If TTBR0 is disabled early (EPD0=1), any further low-stack use faults.
 *
 * This trampoline (naked, no prologue) canonicalizes x0 and moves SP into the
 * high-half direct map before tail-calling the real C entry.
 *
 * IMPORTANT: In a naked function, the body must be ONLY inline asm.
 */
__attribute__((naked, section(".text._kcrt0"), used))
void _kcrt0(void *boot_info) {
    __asm__ volatile(
        /* x0 already holds boot_info; do not touch it except canonicalization. */

        /* Canonicalize x0 for 48-bit VA: sign-extend bit 47 into top 16 bits. */
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

void _kcrt0_c(const boot_info_t *boot_info) {
    kmain((void *)boot_info);

    /* Should not return. Enter low-power wait if it does. */
    for (;;) {
        __asm__ volatile("wfe");
    }
}
