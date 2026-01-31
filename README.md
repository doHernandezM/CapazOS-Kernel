# CapazOS Kernel (AArch64 bring-up)

CapazOS is a from-scratch OS experiment motivated by modern SoCs (especially Apple Silicon) where heterogeneous compute blocks (P/E CPU cores, GPU, Neural Engine-class accelerators, media engines, secure enclaves, etc.) should be treated as first-class system resources.

This repository currently targets **AArch64 under QEMU (`virt`)** to establish a secure kernel mechanism layer and a stable Kernel↔Core boundary. The Apple Silicon–specific ideas (intent-driven scheduling across compute blocks, deterministic energy contracts, hardware-rooted identity) are design goals and are not implemented in this bring-up target.

## Build metadata (current)

Generated from `Code/OS/Scripts/buildinfo.ini`:

- Build date: **2026-01-28**
- Build version: **0.4**
- Boot version: **0.0.10**
- Kernel version: **0.0.10** (build **1462**)
- Core version: **0.0.4** (build **51**)
- Machine target: **Virt**

## Concept priorities (what this codebase is optimizing for)

- **Security by architecture**: default-deny mappings, W^X, narrow kernel attack surface.
- **Capability-oriented authority**: explicit handles rather than ambient/global privilege.
- **Strict Kernel↔Core boundary**: Kernel provides mechanisms; Core is the future policy/services layer (Swift-first).

## Repository layout

- `Code/Kernel` — boot + kernel (C + AArch64 assembly)
- `Code/Core` — Core layer (Swift + small C bridge; currently a minimal scaffold)

## Prerequisites

### macOS
- Xcode (workspace is under `Code/Capaz.xcworkspace`)
- LLVM/LLD via Homebrew (scripts default to `/opt/homebrew/opt/llvm/bin` and `/opt/homebrew/opt/lld/bin`)
- QEMU: `qemu-system-aarch64`

If your toolchain paths differ, edit `Code/OS/Scripts/toolchain.env`.

### Linux (Debian/Ubuntu)

```bash
sudo apt-get update
sudo apt-get install -y clang lld llvm make python3 qemu-system-aarch64 gdb-multiarch
```

If your distro does not provide versioned LLVM binaries, set `LLVM_BIN=/usr/bin` and `LLD_BIN=/usr/bin` in `Code/OS/Scripts/toolchain.env`.

## Build

From the repository root:

```bash
./Code/OS/Scripts/build.sh --platform aarch64-virt --config debug --target kernel_c
```

Convenience wrapper (CI/parity):

```bash
./Code/OS/Scripts/ci_build_kernel.sh --config debug
```

Outputs:

- `build/kernel.img` (boot + padding + kernel; use with QEMU `-kernel`)
- Detailed artifacts under `build/aarch64-virt/<debug|release>/kernel_c/`:
  - `boot.elf`, `boot.bin`
  - `kernel.elf`, `kernel.bin`

Note: the build scripts may bump `kernel_build_number` in `Code/OS/Scripts/buildinfo.ini` unless you export `CAPAZ_BUMP_BUILD_NUMBER=0`.

## Boot / run under QEMU

From the repository root after building:

```bash
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -m 256M \
  -nographic \
  -serial mon:stdio \
  -kernel build/kernel.img
```

Expected behavior today is a bring-up oriented boot log (UART/PL011) with early MMU, memory subsystem init, IRQ/timer baseline, and basic threading/scheduler scaffolding.
