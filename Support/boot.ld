/*
 * boot.ld - linker script for the Capaz boot image.
 *
 * This script links a minimal bootstrap binary that executes in the
 * identity‑mapped, low physical address region before the MMU is enabled.
 * It lays out the boot code (.text.boot), read‑only data (.rodata.boot),
 * mutable data (.data.boot), and BSS (.bss.boot) contiguously starting
 * at a fixed physical base. A dedicated boot stack is also reserved.
 *
 * Symbols exported by this script are consumed by boot code (start.S,
 * boot_higherhalf.S and mmu_ttbr1.c when compiled in BOOT_STAGE). The
 * kernel image is loaded separately at a higher‑half virtual address and
 * should not be referenced directly from this script.
 */

ENTRY(_start)

SECTIONS
{
  /* Physical base for QEMU virt is 0x4000_0000. The boot image is
   * identity‑mapped, so . = __phys_base yields the PA of the first
   * instruction executed by the firmware. */
  __phys_base = 0x40000000;
  . = __phys_base;

  /* ---------------- Boot code/text (.text.boot) ---------------- */
  __boot_text_start = .;
  .text.boot : ALIGN(0x1000) {
    *(.text._start)
    *(.text.boot*)
    *(.text.boot_higherhalf*)
  }
  __boot_text_end = .;

  /* ---------------- Boot read‑only data (.rodata.boot) --------- */
  __boot_rodata_start = .;
  .rodata.boot : ALIGN(16) {
    *(.rodata.boot*)
    /* Pull literal pools emitted by boot C code into the boot rodata
     * section to keep them within range of ADRP/ADD pairs. */
    *(.rodata .rodata.* .rodata.cst* .srodata .srodata.*)
  }
  __boot_rodata_end = .;

  /* ---------------- Boot mutable data (.data.boot) ------------- */
  __boot_data_start = .;
  .data.boot : ALIGN(16) {
    *(.data.boot*)
  }
  __boot_data_end = .;

  /* ---------------- Boot BSS (.bss.boot) ----------------------- */
  __boot_bss_start = .;
  .bss.boot (NOLOAD) : ALIGN(16) {
    *(.bss.boot*)
  }
  __boot_bss_end = .;

  /* ---------------- Boot stack ------------------------------- */
  /* Reserve a 64 KiB stack for early boot. The stack is identity
   * mapped and sits after all other boot sections. */
  .bootstack (NOLOAD) : ALIGN(0x1000) {
    __boot_stack_bottom = .;
    . += 0x10000; /* 64KiB */
    __boot_stack_top = .;
  }

  /* End of the boot image. Useful for image size calculations. */
  __boot_end = .;
}