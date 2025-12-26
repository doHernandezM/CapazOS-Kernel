.section .text.boot_higherhalf, "ax"
.align 2
.global boot_higherhalf
.type boot_higherhalf, %function

// Phase 3.1 higher-half bring-up:
//   - run in physical/identity space with MMU off (boot stack already set)
//   - clear .bss (physical) so C code can safely use static state
//   - install VBAR_EL1 to physical vectors (for faults during bring-up)
//   - build TTBR0 identity "boot" tables + TTBR1 higher-half kernel tables
//   - enable MMU (execution continues via TTBR0 identity mappings)
//   - branch to higher-half entry (execution switches to TTBR1 mappings)

.extern mmu_kernel_init_global
.extern crt0

.extern __bss_start_phys
.extern __bss_end_phys

.extern __stack_top         // higher-half VA
.extern vectors_phys        // physical VA
.extern vectors             // higher-half VA

boot_higherhalf:
    // Install physical vector table before we touch MMU.
    ldr x0, =vectors_phys
    msr vbar_el1, x0
    isb

    // ---- Clear .bss using physical addresses (MMU is still off) ----
    ldr x0, =__bss_start_phys
    ldr x1, =__bss_end_phys
    mov x2, xzr
1:
    cmp x0, x1
    b.hs 2f
    str x2, [x0], #8
    b 1b
2:

    // Build/install page tables and enable MMU (keeps WXN enabled).
    bl  mmu_kernel_init_global

    // Switch to the higher-half kernel stack.
    ldr x0, =__stack_top
    mov sp, x0

    // Switch VBAR to the higher-half vectors.
    ldr x0, =vectors
    msr vbar_el1, x0
    isb

    // Enter C runtime (now executing in TTBR1 region).
    ldr x0, =crt0
    blr x0

3:  wfe
    b   3b
