# rPi3 QEMU Bring-up Notes

This document tracks the rPi3 (`raspi3b`) bring-up direction for CapazOS and keeps it aligned with the `virt` platform architecture.

## Scope

- Target environment: QEMU `raspi3b`
- Keep shared kernel code common between `virt` and `rpi3`
- Keep board-specific behavior isolated in board/arch/HAL layers
- Preserve `virt` runtime behavior while expanding `rpi3`

## Platform model

The platform split is:
- `arch`: CPU and exception-level behavior
- `board`: memory map, interrupt wiring, UART base, timers, and device topology
- `hal`: stable kernel-facing interfaces (`uart`, `timer`, `irq`, `mmu`, `block`, `clock`, `mailbox`)

Generic kernel code should call HAL interfaces only and avoid board symbols directly.

## rPi3 bring-up checklist

1. Boot and early UART
- Enter EL1 cleanly
- Set stack and vectors
- Bring up PL011 UART and print boot banner

2. MMU and memory map
- Define rPi3 RAM/MMIO layout
- Build and install early page tables
- Validate kernel text/data/stack mappings

3. Timer and interrupt routing
- Bring up generic timer
- Route interrupts through board IRQ controller integration
- Confirm periodic and one-shot timer behavior

4. Block device path
- Provide HAL block backend for rPi3/QEMU path
- Verify stable sector reads and error handling

5. Boot media and loader handoff
- Keep loader handoff structure aligned with kernel expectations
- Ensure DTB pointer handoff is valid and consistent

## Validation targets

- `aarch64-virt` still boots and reaches Core console
- `aarch64-rpi3` reaches UART boot logs and kernel handoff
- Shared HAL interfaces remain unchanged for generic kernel subsystems

## Notes

- This document is intentionally implementation-focused and avoids release scheduling language.
- Loader storage policy and filesystem choices are documented in loader-specific sources and README.
