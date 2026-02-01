# CapazOS (Build 1710) — AArch64 Kernel Bring-up

CapazOS is a from-scratch OS experiment motivated by modern SoCs (especially Apple Silicon) where heterogeneous compute blocks (P/E CPU cores, GPU, Neural Engine–class accelerators, media engines, secure enclaves, etc.) should be treated as first-class system resources with explicit intent and stronger structural guarantees.

**Current focus:** establish a small, capability-oriented kernel “mechanism layer” and a stable Kernel↔Core ABI boundary.

**Current target:** **AArch64 under QEMU (`virt`)**. Apple Silicon–specific scheduling/power/security concepts are design goals and are **not implemented yet** in this bring-up target.

---

## Build metadata

Source: `Code/OS/Scripts/buildinfo.ini`

- Build version: **0.6.0**
- Build date: **2026-01-31**
- Kernel: **0.0.10.4** (build **1710**) — `aarch64`, machine **Virt**
- Core: **0.7.0** (name: **Capaz**)

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

### Capability-scoped IPC (bring-up)
- Endpoint capabilities with send/recv rights
- Fixed-size message payloads (inline copy into kernel-owned message objects)
- Blocking receive with wakeup

### Kernel↔Core boundary (Swift-friendly)
- Documented, POD-only boundary rules (`OS/Kern/ABI/BoundaryRules.md`)
- Services tables exposed to Core:
  - v1: logging/panic/alloc/free/time/IRQ primitives/yield
  - v3: cap ops + IPC entrypoints
- Core currently contains a minimal `core_main()` that logs and returns

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

## Near-term milestones (suggested next steps)

1. **User space boundary**
   - Add EL0 tasks/processes, user memory isolation, and a minimal syscall boundary
   - Make capabilities the only way user space can access kernel objects

2. **MEMOBJ + shared memory IPC**
   - Implement a `MEMOBJ` capability with mapping rights
   - Extend IPC to transfer capabilities and/or pass shared-memory descriptors

3. **Preemptive scheduling (single CPU)**
   - Turn on preemption using the existing timer + `sched_irq_exit()` hook
   - Add basic priority/deadline metadata (still CPU-only) as a stepping stone to “intent”

4. **Core as the policy layer**
   - Move bootstrap services out of kernel seeding and into Core
   - Introduce a minimal “service registry” concept in Core on top of endpoint caps

---

## Repository layout

- `Code/OS/Kern` — boot + kernel (C + AArch64 asm)
- `Code/OS/Core` — Core layer (Swift + small C shims)
- `Code/OS/Kern/ABI` — boundary headers + boundary rules

---

## Build

From the repository root:

```bash
./Code/OS/Scripts/build.sh --platform aarch64-virt --config debug --target kernel_c
```

CI/parity wrapper:

```bash
./Code/OS/Scripts/ci_build_kernel.sh --config debug
```

Outputs:

- `build/kernel.img` (boot + padding + kernel; use with QEMU `-kernel`)
- Detailed artifacts under `build/aarch64-virt/<debug|release>/kernel_c/`

Note: build scripts may bump `kernel_build_number` in `Code/OS/Scripts/buildinfo.ini` unless you export `CAPAZ_BUMP_BUILD_NUMBER=0`.

---

## Run under QEMU

After building, from the repository root:

```bash
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -m 256M \
  -nographic \
  -serial mon:stdio \
  -kernel build/kernel.img
```

Expected behavior today is a bring-up oriented boot log (UART/PL011) with early MMU init, PMM init, IRQ/timer baseline, capability/IPC selftests (debug builds), and a minimal Core entry (`core_main()`).
