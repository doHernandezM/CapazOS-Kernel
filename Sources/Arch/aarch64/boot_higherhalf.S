.section .text.boot_higherhalf, "ax"
.align 2
.global boot_higherhalf
.type boot_higherhalf, %function

// Phase 3.1 higher-half bring-up:
//   - run in physical/identity space with MMU off (boot stack already set)
//   - clear .bss (physical) so C code can safely use static state
//   - keep VBAR_EL1 on identity-mapped boot_vectors until switching to TTBR1
//   - build TTBR0 identity "boot" tables + TTBR1 higher-half kernel tables
//   - enable MMU (execution continues via TTBR0 identity mappings)
//   - branch to higher-half entry (execution switches to TTBR1 mappings)

.extern mmu_bootstrap
.extern crt0

.extern __bss_start_phys
.extern __bss_end_phys

.extern __kern_offset

.extern __stack_top_phys      // physical stack top
.extern boot_vectors          // identity-mapped boot vectors
.extern vectors               // higher-half EL1 vector table

boot_higherhalf:
    // Keep an identity-mapped vector table while we are still executing in the
    // low/identity region. The full kernel vectors are linked at the higher-half
    // VMA, so executing them at a low VA (or with MMU off) is unsafe.
    ldr x0, =boot_vectors
    msr vbar_el1, x0
    isb

    // ---- Clear .bss using physical addresses (MMU is still off) ----
    adrp x0, __bss_start_phys
    add  x0, x0, :lo12:__bss_start_phys
    adrp x1, __bss_end_phys
    add  x1, x1, :lo12:__bss_end_phys
    mov x2, xzr
1:
    cmp x0, x1
    b.hs 2f
    str x2, [x0], #8
    b 1b
2:

    // Enable FP/SIMD so later C code (or compiler-emitted instructions) doesn't
    // trap unexpectedly.
    mrs x0, cpacr_el1
    orr x0, x0, #(3 << 20)     // FPEN = 0b11
    msr cpacr_el1, x0
    isb

    // Build/install page tables and enable MMU (keeps WXN enabled).
    bl  mmu_bootstrap

    // Switch to the higher-half kernel stack: sp = __stack_top_phys + __kern_offset
    adrp x0, __stack_top_phys
    add  x0, x0, :lo12:__stack_top_phys
    ldr x1, =__kern_offset
    add x0, x0, x1
    mov sp, x0

    // Switch VBAR to the higher-half vectors (TTBR1 mappings).
    adrp x0, vectors
    add  x0, x0, :lo12:vectors
    msr vbar_el1, x0
    isb

    // Enter C runtime (now executing in TTBR1 region).
    adrp x0, crt0
    add  x0, x0, :lo12:crt0
    blr x0

3:  wfe
    b   3b

    .align 3
    .ltorg
