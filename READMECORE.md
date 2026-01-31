# CapazOS Core

Core is the intended **policy and services layer** for CapazOS. The long-term goal is a Swift-first environment where OS behavior (lifecycle, permissions, service discovery, resource/power intent) is expressed in a higher-level, auditable layer, while the Kernel stays a small mechanism substrate.

On this bring-up target (AArch64 QEMU `virt`), Core is currently a **minimal scaffold**: it can be compiled into relocatable objects and is wired for a strict C ABI boundary, but it is **not yet linked into the running kernel image**.

## Build metadata (current)

Generated from `Code/OS/Scripts/buildinfo.ini`:

- Build date: **2026-01-28**
- Build version: **0.4**
- Kernel version: **0.0.10** (build **1462**)
- Core version: **0.0.4** (build **51**)

## What “Core” is for

Design intent (not all implemented yet):

- **Capability-scoped services**: discovery and IPC routed through explicit authority.
- **Declarative lifecycle**: explicit app/service states with enforced resource ceilings.
- **Cost visibility**: make performance/power/privacy costs visible at build time and runtime.

## Build (standalone Core objects)

Core builds into two relocatable objects:

- `core_c.o` — C sources from `Code/OS/Core/*.c`
- `core_swift.o` — Swift sources from `Code/OS/Core/*.swift`

From the repository root:

```bash
./Code/Core/Scripts/build_core.sh \
  --platform aarch64-virt \
  --config debug \
  --out build/aarch64-virt/debug/core \
  --kernel-abi ./Code/OS/Kern/ABI
```

Outputs:

- `build/aarch64-virt/debug/core/core_c.o`
- `build/aarch64-virt/debug/core/core_swift.o`

Notes:

- The build enforces a simple rule: Core should include **only** ABI headers (via `--kernel-abi`).
- Toolchain paths are configured in `Code/Core/Scripts/toolchain.env` (must provide `SWIFTC` for Swift builds).

## Current integration status

The kernel build (`Code/OS/Scripts/build.sh`) currently produces `build/kernel.img` without linking Core. The Kernel↔Core ABI and services-table patterns are in place to support linking and calling into Core once the loader/link contract is finalized.
