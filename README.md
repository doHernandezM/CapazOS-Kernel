# CapazOS — AArch64 Kernel + Core Bring-up

CapazOS is a from-scratch OS experiment motivated by modern SoCs (especially Apple Silicon) where heterogeneous compute blocks (P/E CPU cores, GPU, Neural Engine–class accelerators, media engines, secure enclaves, etc.) should be treated as first-class system resources with explicit intent and stronger structural guarantees.

**Current focus:** a small, capability-oriented kernel “mechanism layer” with a Swift Core policy layer over capability-scoped IPC.

**Current target:** **AArch64 under QEMU (`virt`)**. Apple Silicon–specific scheduling/power/security concepts are design goals and are **not implemented yet** in this bring-up target.

---

## Roadmap (rPi3 QEMU bring-up)

We are preparing a **raspi3b QEMU** bring-up to make rPi3 a first-class platform alongside `virt`. The multi-milestone task list lives in `Docs/Plan-RPi3.md`.

---

## Build metadata

Source: `Scripts/buildinfo.ini` (compiled into `buildinfo.h`).

- Kernel prints build number and date at boot.
- Core can query build metadata via `cka_get_build_info()`.

---

## What exists today (implemented mechanisms)

### Boot + platform bring-up (QEMU virt)
- AArch64 boot to EL1, early UART/PL011 console
- DTB parsing for basic platform discovery (e.g., memory ranges, UART base)
- MMU setup (high-half kernel mapping) + basic physical memory manager (bitmap PMM)
- Interrupt controller bring-up (**GICv2**) and architected generic timer

### Kernel scheduling + execution contexts
- Minimal **cooperative** scheduler (round-robin) with preemption scaffolding
- Thread objects + per-thread kernel stacks
- Clear context contracts: “IRQ context cannot allocate/block/call Core”
- Deferred work queue to move work out of interrupt context

### Capability-oriented authority (early)
- Capability table with generation counters (stale-handle invalidation)
- Capability rights model (dup/transfer/drop/invalidate)
- Initial object types: task, thread, endpoint, memobj (reserved), irq/timer tokens, service

### Capability-scoped IPC
- Endpoint capabilities with send/recv rights
- Fixed-size message payloads (inline copy into kernel-owned message objects)
- Blocking receive with wakeup

### Kernel↔Core boundary (Swift-friendly)
- Documented, POD-only boundary rules (`OS/Docs/BoundaryRules.md`)
- Core boots via a stable C entrypoint and hands off to Swift
- Core receives boot-seeded endpoint caps (UART cmd/evt, KernelLog)
- Core implements a minimal console policy layer and prompt

### Console and serial I/O
- Kernel provides a UART driver service (TX via cmd endpoint, RX via event endpoint)
- Core implements the Console policy, line editing, and basic commands
- `devices` enumerates DTB nodes via the Kernel API

---

## What is NOT implemented yet (major gaps vs the concept)

These are the core “Apple Silicon OS” ideas that remain **design-only** at this stage:

- **Intent-driven heterogeneous scheduling** across CPU/GPU/NPU/media engines
- **Unified memory locality management** across compute blocks (beyond basic kernel mapping)
- **Energy contracts / deterministic power budgeting** and OS-enforced power allocation
- **Hardware-rooted identity and sealing** (Secure Enclave, pointer auth, secure boot chains, attestation)
- QEMU `virt` does not model SEP/AMX/ANE/ISP/media engines; those integrations will be Apple-Silicon-specific
- **Sandboxed user space** with a real process model, user/kernel isolation policy, and mandatory capability boundaries
- **Driver model redesign** (user-space drivers, device virtualization, strong contracts)
- **POSIX compatibility layer** (not started)
- **Data ownership model** (object-based storage, built-in sync/backup/encryption policies)
- **Declarative service discovery / structured IPC policy** (beyond low-level endpoints)
- **Unified UI / scene graph** (SwiftUI-like) and “one kernel, many shells” policy layer
- **Developer-facing power/privacy/perf cost tooling** (static analysis, build-time checks)

---

## Status vs the feature set you described

Progress here is best understood as **“kernel substrate readiness,”** not as a percent of the end-state OS.

### Already underway (foundational)
- Smaller kernel with explicit mechanism/policy split (Kernel↔Core ABI boundary)
- Capability model (handles + rights + revocation via generation)
- Capability-scoped IPC primitives
- Clear execution-context contracts (IRQ vs thread) that future policy can build on

### Partially represented (scaffolding exists, policy missing)
- Scheduling: cooperative scheduler + preemption hooks exist, but no intent model
- Resource governance: some object types exist (task/thread/token/memobj reserved), but no real resource accounting
- “Security by architecture”: attack-surface reduction principles are visible, but there is no user space yet

### Not started (end-state features)
- Heterogeneous compute scheduling + unified memory decisions
- Energy contracts and power budgeting
- Data/object model, POSIX layer, UI system, multi-device shells/policies
- Hardware trust integration (SEP / secure boot / attestation)

---

## Repository layout

- `Code/OS/Kern` — boot + kernel (C + AArch64 asm)
- `Code/OS/Core` — Core layer (Swift + small C shims)
- `Code/OS/Docs` — design docs (boundary rules, etc.)
- `Code/OS/Docs/Plan-RPi3.md` — rPi3 QEMU bring-up task list
- `Code/OS/Kern/Kernel/api` — internal Core↔Kernel API surface

---

## Build

From the repository root:

```bash
./Scripts/build.sh --virt
```

Or explicitly:

```bash
./Scripts/build.sh --platform aarch64-virt
```

Note: `--platform aarch64-rpi3` is planned and tracked in `Docs/Plan-RPi3.md`.

CI/parity wrapper:

```bash
./Scripts/ci_build_kernel.sh
```

Outputs:

- `build/Kernel.img` (boot + padding + kernel; use with QEMU `-kernel`)
- Detailed artifacts under `build/aarch64-virt/<debug|release>/kernel_c/`

Note: build scripts may bump `kernel_build_number` in `Code/OS/Scripts/buildinfo.ini` unless you export `CAPAZ_BUMP_BUILD_NUMBER=0`.

---

## Run under QEMU

After building, from the repository root:

### Virt
```bash
qemu-system-aarch64 \
-machine virt,gic-version=2 \
-cpu cortex-a53 -smp 2 \
-m 128M \
-nographic \
-serial stdio \
-monitor none \
-kernel build/Kernel.img \
-drive if=none,file=/Users/cosas/CapazOS/disk_fat16.img,format=raw,id=hd0 \
-device virtio-blk-device,drive=hd0 \
-device virtio-rng-device
```

### Raspbery Pi 3
```bash
qemu-system-aarch64 \
-machine raspi3b \
-cpu cortex-a53 \
-m 1G \  
-nographic \
-serial mon:stdio \
-kernel build/kernel.img
-drive if=none,file=/Users/cosas/CapazOS/disk_fat16.img,format=raw,id=hd0 \
-device virtio-blk-device,drive=hd0 \
-device virtio-rng-device
```

Expected behavior today is a bring-up oriented boot log (UART/PL011) with early MMU init, PMM init, IRQ/timer baseline, and a Core console prompt.
