# rPi3 QEMU Bring-up Plan (raspi3b)

This plan targets **QEMU `-machine raspi3b`**, not physical hardware. The goal is to make rPi3 a first-class platform alongside `virt` with a clean board abstraction and a repeatable boot flow.

## Scope and assumptions

- QEMU `raspi3b` only for this phase.
- Kernel image is loaded as `KERNEL*.IMG` from a FAT32 boot partition.
- AArch64 EL1 kernel, with board-specific init in `Start.S` and rPi-specific linker script.
- Platform work should not regress `virt`.

## Milestone 0 — Build discipline and platform abstraction

Goal: Treat `virt` as one of many platforms and introduce a clear board HAL surface.

Tasks:
1. Enforce explicit platform selection in `Scripts/build.sh` (e.g. `--platform aarch64-virt`, `--platform aarch64-rpi3`).
2. Split platform config into `arch` and `board` layers.
3. Introduce a minimal HAL surface for `uart`, `timer`, `irq`, `mmu`, `block`, `clock`, `mailbox`.
4. Keep `virt` booting with the new HAL surface.

Exit criteria:
1. `virt` still boots and prints UART output through the HAL.
2. Build output includes platform-specific directories.

## Milestone 1 — rPi3 boot flow + early UART

Goal: Reach early serial output on QEMU raspi3b.

Tasks:
1. Add rPi3 board target with linker script and load address.
2. Implement rPi entry point in `Start.S` (EL detection, stack, vector base).
3. Bring up early UART (PL011 preferred for QEMU raspi3b).
4. Install minimal exception vectors (sync and IRQ stubs).

Exit criteria:
1. Kernel prints a banner on boot under QEMU raspi3b.
2. No MMU yet, identity-mapped physical addressing.

## Milestone 2 — MMU + page tables

Goal: Enable MMU using the rPi3 memory map.

Tasks:
1. Define rPi3 memory map and MMIO regions.
2. Build early page tables and enable MMU at EL1.
3. Validate kernel text/data, stacks, and early allocations under MMU.

Exit criteria:
1. Kernel continues logging after MMU enable.
2. PMM and early heap remain stable.

## Milestone 3 — Timer + IRQ wiring

Goal: Establish periodic interrupts and scheduler tick.

Tasks:
1. Initialize the ARM generic timer.
2. Wire the GIC and enable IRQs.
3. Add a minimal timer interrupt handler and scheduler tick.

Exit criteria:
1. Timer interrupts fire at a stable interval.
2. Scheduler tick and yield path are observable.

## Milestone 4 — SD/MMC block device

Goal: Replace virtio block with rPi SD/MMC.

Tasks:
1. Implement an rPi SD/MMC driver (PIO path first).
2. Add block device abstraction to the HAL.
3. Read a known sector and validate signature or checksum.

Exit criteria:
1. Block reads succeed from the SD/MMC device in QEMU.

## Milestone 5 — FAT32 boot partition

Goal: Match the real firmware boot flow.

Tasks:
1. Build a FAT32 boot partition with `KERNEL*.IMG`.
2. Verify QEMU raspi3b boots from this image.
3. Add minimal FAT32 read support if kernel needs modules/resources.

Exit criteria:
1. QEMU raspi3b boots from the FAT32 image and reaches the console.

## Milestone 6 — Mailbox + clock setup

Goal: Reliable clocks for UART and timers.

Tasks:
1. Implement mailbox property interface.
2. Set UART clock and core clock via mailbox.
3. Re-verify UART baud and timer accuracy.

Exit criteria:
1. UART baud remains stable across reboots.
2. Timer interval stays accurate.

## Milestone 7 — Platform hardening and CI

Goal: Make rPi3 a clean, supported platform target.

Tasks:
1. Extract rPi3-specific code into a dedicated platform folder.
2. Keep the HAL surface stable and minimal.
3. Add a QEMU raspi3b boot test in CI or local script.

Exit criteria:
1. `--platform aarch64-rpi3` is a supported, documented target.
2. `virt` and `rpi3` both boot with shared kernel code.
