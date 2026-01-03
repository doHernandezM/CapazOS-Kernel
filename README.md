# CapazOS Kernel (AArch64)

## Concept

CapazOS is an educational, freestanding **AArch64** kernel built to explore a modern OS design in small, testable milestones under **QEMU `virt`**, then eventually on real hardware.

The long-term design intent is:

- **Capability-based security model**
  - fine-grained authority (object capabilities) instead of ambient privileges
  - explicit delegation and revocation strategies (eventually)
  - a small trusted computing base and strong isolation boundaries

- **Energy-aware system design**
  - predictable low-power idle paths (WFI/WFE discipline)
  - timer/interrupt design that supports deep sleep
  - later: DVFS hooks and energy-aware scheduling policies

- **Minimal, comprehensible bring-up**
  - higher-half kernel mapping from the start
  - DTB-driven hardware discovery (UART, RAM, interrupts, timers)
  - incremental transition to real drivers, interrupts, and a scheduler

## Repository layout

This repo builds **two images**:

- **Boot stage** (`Kernel/Sources/Arch/aarch64/start.S`)
  - runs at EL1 under QEMU
  - sets up a temporary stack and early exception vectors
  - creates minimal translation tables and enables the MMU
  - locates the kernel image in RAM, constructs a `boot_info`, and branches to the kernel entry

- **Kernel stage** (C + assembly)
  - `_kcrt0` trampoline moves the stack into the higher-half direct map and tail-calls C
  - `kmain()` installs kernel vectors, initializes the kernel MMU tables, initializes UART, and prints status
  - DTB parsing is being brought up to feed platform data into subsystems

Artifacts are written to `build/` at the repo root:
- `build/boot.elf`, `build/boot.bin`
- `build/kernel.elf`, `build/kernel.bin`
- `build/kernel.img` (boot + padding + kernel; passed to QEMU as `-kernel`)

## Current status

As of **Boot/Kernel 0.0.3**:

- Stable bring-up to the kernel stage (MMU enabled, vectors installed).
- `boot_info` includes:
  - kernel physical base, kernel image size, entry offset
  - **DTB virtual address** (in the higher-half direct map) and DTB size
- DTB parsing currently:
  - reads the DTB header and prints a summary
  - extracts at least one RAM range (QEMU commonly reports `0x4000_0000` + size)
  - reserved ranges are not yet fully collected
  - UART discovery via DTB is **not finished** (hardcoded PL011 base is used as fallback)

Work in progress (next milestone) is to make DTB results *actionable* by kernel subsystems:
- resolve `/chosen/stdout-path` → `/aliases` → UART node → `reg` → MMIO base
- collect reserved ranges (`memreserve`, `/reserved-memory`, plus implicit reservations like kernel + DTB)
- compute **usable RAM ranges** = memory ranges minus reserved ranges
- map only DTB-reported RAM spans in `mmu_init()` (instead of a fixed window)

Once usable RAM ranges are reliable, it becomes appropriate to introduce early memory management:
- a small **physical page allocator (PMM)** seeded from usable ranges
- later, a higher-level allocator (slab/heap) built on top of PMM

## Build prerequisites (macOS)

Install dependencies (Homebrew examples):

- LLVM + lld:
  - `brew install llvm lld`
- QEMU:
  - `brew install qemu`
- Python 3:
  - macOS typically ships a usable `python3`, or use `brew install python`
- Optional for debugging:
  - `brew install gdb` (or `gdb-multiarch` if you use it)
  - You can also use LLDB + QEMU remote gdbserver in some setups, but the docs below assume GDB.

The build scripts default to Homebrew paths:
- LLVM tools: `/opt/homebrew/opt/llvm/bin`
- LLD: `/opt/homebrew/opt/lld/bin`

If you are on an Intel Mac or have different install paths, update `Kernel/Scripts/toolchain.env`.

## Build

From the repo root:

```bash
./Kernel/Scripts/build.sh
```

This produces `build/kernel.img` and prints the computed kernel physical/virtual base addresses.

### Optional: enable a deliberate fault test

The build supports an optional macro used by some debugging paths:

```bash
CAPAZ_FAULT_TEST=1 ./Kernel/Scripts/build.sh
```

## Run under QEMU

From the repo root:

```bash
./Kernel/Scripts/run-qemu.sh
```

Defaults:
- machine: `virt`
- CPU: `cortex-a72`
- memory: `512M`
- serial: `stdio`

### Run with a GDB stub

```bash
DEBUG_PORT=1234 ./Kernel/Scripts/run-qemu.sh
```

This adds `-S -gdb tcp::1234` so QEMU halts at reset and waits for a debugger.

## Debug with GDB

In one terminal (QEMU, halted):

```bash
DEBUG_PORT=1234 ./Kernel/Scripts/run-qemu.sh
```

In another terminal:

```bash
gdb-multiarch build/boot.elf
(gdb) target remote :1234
(gdb) b _start
(gdb) c
```

Notes:
- The earliest breakpoint is typically `_start` in `start.S`.
- You can also break on `_kcrt0` and `kmain` once the kernel stage is reached.

## Long-term goals

The “north star” is a small, capability-oriented kernel that can evolve into:

- **Boot to userspace** with a minimal init process
- **DTB-driven driver bring-up**
  - UART from `/chosen/stdout-path`
  - GIC interrupt controller, ARM timer, PSCI, virtio devices (QEMU), etc.
- **Memory management**
  - physical memory manager (PMM)
  - virtual memory subsystem with page fault handling
  - kernel heap and object allocators
- **Capability system**
  - kernel objects addressed via capabilities
  - message-passing IPC with explicit authority transfer
  - per-process address spaces and resource accounting
- **Power management**
  - structured idle states, tickless scheduling (later)
  - timer coalescing and interrupt discipline
  - hooks for DVFS/governors on real hardware (later)

## License

BSD 2-Clause. See `LICENSE`.
