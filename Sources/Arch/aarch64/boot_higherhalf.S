.section .text.boot_higherhalf, "ax"
.align 2
.global boot_higherhalf
.type boot_higherhalf, %function

// Preprocessed assembly (.S) file.  We keep the canonical higher‑half
// virtual base defined locally because including vm_layout.h in assembly
// drags in inline C code.  KERNEL_VA_BASE must match the higher‑half
// base used by the kernel (0xFFFFFF8040000000).  We no longer rely on
// a fixed offset from a hard-coded physical base; instead we compute
// the virtual address as KERNEL_VA_BASE + (pa - KERNEL_PHYS_BASE) at
// runtime.
    .equ    KERNEL_VA_BASE, 0xffffff8040000000

/*
 * The boot configuration constants (e.g. KERNEL_PHYS_BASE, KERNEL_PHYS_END,
 * KERNEL_ENTRY_OFFSET) are provided to the assembler via -D options
 * passed in from the build script.  We intentionally do not include
 * boot_config.h here because that header defines its defaults using
 * `ull` suffixes, which the assembler treats as symbol names rather
 * than numeric literals.  Instead, the build script supplies
 * KERNEL_PHYS_BASE, KERNEL_PHYS_END and KERNEL_ENTRY_OFFSET directly
 * to the assembler.  KERNEL_VA_BASE is defined above.
 */

// Phase 3.1 higher-half bring-up:
//   - run in physical/identity space with MMU off (boot stack already set)
//   - clear .bss (physical) so C code can safely use static state
//   - keep VBAR_EL1 on identity-mapped boot_vectors until switching to TTBR1
//   - build TTBR0 identity "boot" tables + TTBR1 higher-half kernel tables
//   - enable MMU (execution continues via TTBR0 identity mappings)
//   - branch to higher-half entry (execution switches to TTBR1 mappings)

.extern mmu_bootstrap
// Do not declare crt0_phys here because the boot image does not
// reference kernel symbols directly.  The entry offset is provided
// via KERNEL_ENTRY_OFFSET above.

// The boot image clears its own BSS section before calling into C code.
// Use the boot-stage BSS symbols defined by the boot linker script.
.extern __boot_bss_start
.extern __boot_bss_end

// Boot vectors are identity-mapped; keep using them until the kernel
// installs its own exception vectors.  Do not reference the higher-half
// vectors table from boot code.
.extern boot_vectors

boot_higherhalf:
    // Keep an identity-mapped vector table while we are still executing in the
    // low/identity region. The full kernel vectors are linked at the higher-half
    // VMA, so executing them at a low VA (or with MMU off) is unsafe.
    ldr x0, =boot_vectors
    msr vbar_el1, x0
    isb

    // ---- Clear the boot-stage BSS (MMU is still off) ----
    // The kernel image's BSS will be cleared by crt0 in the higher-half.
    adrp x0, __boot_bss_start
    add  x0, x0, :lo12:__boot_bss_start
    adrp x1, __boot_bss_end
    add  x1, x1, :lo12:__boot_bss_end
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

    // Switch to the higher‑half kernel stack.  The higher‑half stack
    // virtual address is computed as:
    //   sp = KERNEL_VA_BASE + (KERNEL_PHYS_END - KERNEL_PHYS_BASE)
    // where KERNEL_PHYS_END and KERNEL_PHYS_BASE are supplied by
    // the build script.  This avoids assuming a fixed physical load
    // address when computing the VA offset.
    ldr x0, =KERNEL_PHYS_END   // physical end of the kernel image (top of stack)
    ldr x1, =KERNEL_PHYS_BASE  // physical base of the kernel image
    sub x0, x0, x1            // x0 = offset from image base to kernel end
    ldr x1, =KERNEL_VA_BASE          // higher‑half VA base
    add sp, x1, x0                  // sp = KERNEL_VA_BASE + offset

    // Keep using the identity‑mapped boot vector table.  The kernel
    // proper will install its own higher-half vectors in crt0 once
    // higher-half execution begins.  Avoid referencing the kernel
    // vector table from the boot image.

    // --------------------------------------------------------------------
    // Prepare the boot→kernel handoff.  The higher-half kernel expects a
    // pointer to a `boot_info_t` structure in x0.  Allocate this struct on
    // the kernel stack (TTBR1) so that it is mapped into the higher-half
    // address space.  Populate the fields:
    //   - kernel_phys_base: physical address of the kernel image (start of
    //     __text segment)
    //   - kernel_va_base:   virtual address at which the kernel is mapped
    //   - kernel_entry_va:  virtual address of the crt0 entry point
    //   - reserved: zeroed for future use

    // Reserve space for boot_info_t (8 qwords) on the stack.  Each field
    // is 8 bytes, so allocate 64 bytes.  sp currently points at
    // __stack_top (higher-half) which was computed above.
    sub   sp, sp, #64
    mov   x3, sp              // x3 = pointer to boot_info_t (VA)

    // Compute kernel_phys_base (PA of the kernel image base).  Use
    // KERNEL_PHYS_BASE from boot_config.h instead of taking the address
    // of kernel linker symbols.
    ldr   x4, =KERNEL_PHYS_BASE
    str   x4, [x3, #0]        // boot_info->kernel_phys_base = KERNEL_PHYS_BASE

    // Compute kernel_va_base.  The kernel is mapped at a fixed
    // higher‑half virtual base independent of its physical load address.
    // KERNEL_VA_BASE is defined above and supplied by the build script.
    ldr   x6, =KERNEL_VA_BASE
    str   x6, [x3, #8]        // boot_info->kernel_va_base = KERNEL_VA_BASE

    // Compute kernel_entry_va = KERNEL_VA_BASE + KERNEL_ENTRY_OFFSET.
    ldr   x7, =KERNEL_ENTRY_OFFSET
    add   x7, x6, x7
    str   x7, [x3, #16]       // boot_info->kernel_entry_va = x7

    // Zero the reserved fields (5×8 bytes)
    mov   x8, xzr
    str   x8, [x3, #24]
    str   x8, [x3, #32]
    str   x8, [x3, #40]
    str   x8, [x3, #48]
    str   x8, [x3, #56]

    // Set x0 to the pointer to boot_info_t (already a higher‑half VA)
    mov   x0, x3

    // Branch to the kernel entry.  x0 holds a valid boot_info_t* and
    // x7 holds the kernel_entry_va.  Copy x7 into x1 and branch.  The
    // call never returns.
    mov   x1, x7
    br    x1

3:  wfe
    b   3b

    .align 3
    .ltorg
