# CapazOS (AArch64) — Boot + Kernel + Core (scaffold)

## Concept

CapazOS is an educational, freestanding **AArch64** operating system project designed around:

- **Security by construction** (small trusted base, default-deny mappings, W^X)
- **A modern capability model** (explicit authority, no ambient “global” privileges)
- **A Swift-first “Core” layer** (policy and services in Swift later; kernel mechanisms kept minimal)

The current focus is establishing a **secure kernel mechanism layer** under **QEMU `virt`** that is stable enough to host a higher-level **Core** layer (initially C, then progressively Swift).

## Repository layout

This workspace contains **two projects**:

- **Kernel** (C + assembly)
  - Builds two images: **Boot** + **Kernel**
  - Runs on QEMU `qemu-system-aarch64 -machine virt -cpu cortex-a72`
- **Core** (Swift)
  - Present as a project scaffold (intentionally minimal right now)
  - Later becomes the primary home for policy, services, and higher-level OS logic

### Boot/Kernel two-image design

The system builds **two separately-linked artifacts**:

- **Boot stage**
  - minimal EL1 setup, boot-only vectors, boot-only BSS clear
  - constructs coarse page tables and enables the MMU
  - validates/preserves the DTB pointer and hands a single `boot_info_t*` to the kernel in `x0`
- **Kernel stage**
  - owns TTBR1 mappings, disables TTBR0 (default-deny user mapping)
  - installs kernel vectors and enforces runtime **W^X**
  - brings up memory management, interrupts/timers, and the thread/scheduler baseline

## Current status (where we are now)

As of **Boot/Kernel 0.0.8D** (**Build 47**, 2026-01-12):

### Milestone position
- **M1–M4 are effectively complete**:
  - two-image boot/kernel pipeline is stable
  - kernel takes over MMU, disables TTBR0 early (default-deny)
  - runtime **W^X** policy is enforced (text RX, rodata R/NX, data+bss RW/NX, device NX)

### Bring-up + memory/MMU
- Higher-half direct map is active early.
- Kernel rebuilds TTBR1 mappings and installs kernel VBAR.
- DTB parsing is active and used for platform data:
  - `/memory` ranges
  - memreserve / reserved ranges
  - PL011 UART discovery (QEMU `virt`)
- Memory subsystem is online:
  - **PMM (bitmap)** based on DTB-derived usable ranges
  - Allocation tiers (M5.5 direction):
    - **PMM** = pages only
    - **slab caches** = kernel objects only (fixed-size, type-specific)
    - **kheap** = variable-sized buffers only (preferred name: `kbuf_alloc/kbuf_free`)
  - Core may only allocate via `services->alloc/free` (buffer tier), thread-context only

### Interrupts + timers
- Stable interrupt baseline under QEMU `virt`:
  - **GICv2** init + IRQ dispatch
  - **ARM generic timer (CNTV)** programming and tick delivery
- IRQ plumbing exists and is kept separate from synchronous exception reporting (bring-up oriented).

### Threads + scheduling (bring-up oriented)
- Kernel thread structures and AArch64 context switching support exist.
- Scheduler baseline exists (cooperative foundation; preemption hooks are present but not yet “contract-locked”).
- A minimal deadline-queue exists as groundwork for later timer-based scheduling.

### Core (Swift)
- A separate **Core** Xcode project exists but is intentionally minimal.
- Build scripts support producing a Swift object (Embedded mode), but the Swift Core is **not integrated** into the kernel yet.

---

## Roadmap (milestone plan)

This roadmap is designed to support your three goals:
1) a secure OS, 2) a capability OS, 3) as much of Core as possible in Swift (later).

### Milestone M4.5 — Contracts locked (invariants only)

**Goal:** make execution context rules, allocation rules, and W^X assertions explicit and enforced.

Acceptance criteria
- Execution contexts defined and enforced:
  - IRQ context cannot allocate, cannot block, cannot call into Core.
  - Thread context may allocate and may call Core.
- Single allocation policy documented:
  - which allocator is used for what (kernel objects vs buffers).
- W^X invariants asserted with the current image layout:
  - text RX, rodata R/NX, data+bss RW/NX, device NX (plus checks/asserts).

Phases
1. Context framework
   - `in_irq()` indicator + `ASSERT_THREAD_CONTEXT()` / `ASSERT_IRQ_CONTEXT()`
   - wrap allocator entry points with “deny in IRQ” assertions
2. Hardening hooks
   - allocation counters by type, peak heap, PMM pressure
   - poison-on-free (at least one allocator path)

### Milestone M5.0 — Core boundary introduced (C-only Core, no Swift yet)

**Goal:** introduce a strict kernel↔core seam so policy can move out of kernel cleanly later.

Acceptance criteria
- A separate Core module is linked into the kernel image (still C at this stage).
- Core code is placed in dedicated sections:
  - `.text.core/.rodata.core/.data.core/.bss.core`
- Kernel calls `core_main()` exactly once on boot.
- Core uses the Kernel Services ABI only (no internal kernel headers).

Phases
1. Section split in linker + MMU classifier updates
2. Kernel Services ABI v1 introduced (`kernel_services_v1_t`)
3. Restrict Core’s include path to enforce the boundary

### Milestone M5.5 — Allocator truth established (hybrid model)

**Goal:** make memory allocation predictable and safe enough for Core growth (and later Swift).

Acceptance criteria
- One public allocation surface for Core (`services->alloc/free`) defined as **thread-context only**.
- Kernel objects use type-specific allocation (slabs/caches) for at least:
  - threads/tasks
  - capability entries
  - IPC message objects (or endpoint queue nodes)
- Variable-sized buffers use `kheap` (not used for kernel objects).

Phases
1. Define tiers: PMM (pages) → slabs (objects) → kheap (buffers)
2. Convert high-churn objects first (threads, IPC message objects)
3. Add observability: per-cache stats, failure behavior, peak usage

### Milestone M6 — Concurrency baseline for Core

**Goal:** run Core code in a controlled execution model that is safe and debuggable.

Acceptance criteria
- Core runs in one dedicated kernel thread (“core/main”).
- IRQ handlers do top-half only and enqueue work to a deferred-work queue.
- Scheduling policy for this stage is explicit:
  - recommended: cooperative baseline or preemption-disabled while Core runs.

Phases
1. Deferred work queue + worker drain in thread context
2. Core thread lifecycle and controlled entrypoint
3. Explicit preemption/critical-section policy for Core

### Milestone M7 — Capability kernel skeleton (mechanisms)

**Goal:** establish capability mechanisms without turning the kernel into a policy engine.

Acceptance criteria
- Capability storage exists (cap space / table).
- Capabilities carry type + rights.
- Basic operations exist: create, dup/transfer, drop (revocation can be staged).

### Milestone M8 — IPC endpoints + message passing (capability-scoped)

Acceptance criteria
- Endpoints are kernel objects referenced by capabilities.
- Message send/recv works between threads with explicit lifetime rules.
- Capability transfer in messages is staged in once base IPC is stable.

### Milestone M9 — Service registry + discovery (policy in Core)

Acceptance criteria
- Registry maps service IDs → endpoint capabilities.
- Clients obtain service access via explicit lookup (no global ambient namespace).
- Registry and discovery are implemented as Core policy (not kernel mechanism).

### Milestone M10 — Intent descriptors (data model + hooks)

Acceptance criteria
- Intent schema exists (latency/throughput/background/etc.).
- Tasks/threads carry intent descriptors.
- Scheduler accepts intent inputs (policy can remain naive initially).

---

## Build prerequisites

### macOS
- Xcode (workspace-based development)
- Homebrew LLVM + LLD (deterministic external toolchain)

The scripts default to Homebrew paths:
- LLVM tools: `/opt/homebrew/opt/llvm/bin`
- LLD: `/opt/homebrew/opt/lld/bin`

If you are on an Intel Mac or have different install paths, update `Scripts/toolchain.env`.

### Linux
Debian/Ubuntu examples:

- `sudo apt-get update`
- `sudo apt-get install -y clang lld llvm make python3 qemu-system-aarch64 gdb-multiarch`

If your distro installs LLVM tools without versioned paths, you can typically use:
- `LLVM_BIN=/usr/bin`
- `LLD_BIN=/usr/bin`
by editing `Scripts/toolchain.env`.

## Build

From the repo root:

```bash
# Build boot + kernel (combined image for QEMU)
./Scripts/build.sh kernel_c

# If your checkout defaults to kernel build, this usually also works:
./Scripts/build.sh
```

Artifacts are emitted under `build/`:
- `build/boot.elf`, `build/boot.bin`
- `build/kernel.elf`, `build/kernel.bin`
- `build/kernel.img` (boot + padding + kernel; passed to QEMU as `-kernel`)

## Run (QEMU)

If you have the helper script:

```bash
./Scripts/run.sh
```

Manual invocation (typical):

```bash
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -m 128M \
  -nographic \
  -serial mon:stdio \
  -kernel build/kernel.img
```
