# CapazOS

CapazOS is a from-scratch AArch64 OS project that separates low-level kernel mechanisms from higher-level Core policy code.

Current active targets:
- `aarch64-virt` on QEMU `virt`
- `aarch64-rpi3` bring-up sources exist, but `virt` is the primary validated runtime path

## Current project state

Implemented and in active use:
- Platform layering split into architecture, board, and HAL surfaces
- HAL surfaces for `uart`, `timer`, `irq`, `mmu`, `block`, `clock`, and `mailbox`
- Virt bring-up with UART logging, MMU setup, GIC/timer wiring, and block I/O via virtio
- Loader target for `virt` with:
  - GPT partition discovery
  - FAT16 volume parsing
  - ELF kernel loading (`KERNEL.ELF` / `KERNEL8.ELF`)
  - `boot_info` handoff (including DTB pointer when available)
- Kernel + Core filesystem support:
  - Shared FAT parsing support
  - FAT16 and FAT32 read/write integration in Core-facing APIs
  - Format-agnostic FS calls used by Core services

Not implemented yet:
- Hardware trust chain enforcement (secure boot/attestation)
- Apple Silicon heterogeneous intent scheduling across CPU/GPU/NPU blocks
- Power contract and deterministic energy budget model
- User-space driver model and full capability-isolated user process model

## Repository layout

- `Kern/` kernel, arch code, HAL, loader
- `Core/` Swift Core layer and shared filesystem logic
- `Scripts/` build and packaging scripts
- `Docs/` design and bring-up notes

## Build

From `/Users/cosas/CapazOS/Code/OS`:

Build loader and kernel (default):

```bash
./Scripts/build.sh
```

Build only loader:

```bash
./Scripts/build.sh --loader
```

Build only kernel:

```bash
./Scripts/build.sh --kernel
```

Select platform explicitly (default is `aarch64-virt`):

```bash
./Scripts/build.sh --platform aarch64-virt
```

Common outputs in `/Users/cosas/CapazOS/build/`:
- `loader.bin`
- `kernel.img`
- `kernel.elf`

## Run

Kernel image direct boot (`virt`):

```bash
qemu-system-aarch64 \
-machine virt,gic-version=2 \
-cpu cortex-a53 -smp 2 \
-m 128M \
-nographic \
-serial mon:stdio \
-kernel /Users/cosas/CapazOS/build/kernel.img
```

Loader boot (`virt`) with disk image:

```bash
qemu-system-aarch64 \
-machine virt,gic-version=2 \
-cpu cortex-a53 \
-m 128M \
-nographic \
-serial mon:stdio \
-kernel /Users/cosas/CapazOS/build/loader.bin \
-drive if=none,file=/Users/cosas/CapazOS/disk.img,format=raw,id=hd0 \
-device virtio-blk-device,drive=hd0
```

Loader disk requirements today:
- GPT disk
- At least one non-empty GPT partition
- FAT16 filesystem in selected partition
- `KERNEL.ELF` or `KERNEL8.ELF` at FAT root or subpath handled by loader lookup
