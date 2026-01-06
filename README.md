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

## Platform status

| AArch64 Platform | Status | Signal |
|---|---|---|
| QEMU `virt` | Builds | 🟢 green |
| Raspberry Pi | Roadmap | 🟠 orange |
| Apple Silicon | Long term | 🔴 red |

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
  - DTB parsing feeds platform data into subsystems

Artifacts are written to `build/` at the repo root:
- `build/boot.elf`, `build/boot.bin`
- `build/kernel.elf`, `build/kernel.bin`
- `build/kernel.img` (boot + padding + kernel; passed to QEMU as `-kernel`)

## Current status

As of **Boot/Kernel 0.0.6**:

- Stable bring-up to the kernel stage (MMU enabled, kernel vectors installed).
- Higher-half direct map is active early; TTBR0 is default-deny (disabled) in kernel runtime.
- Kernel C runtime zeroes `.bss` deterministically before `kmain()`.
- DTB parsing provides `/memory` ranges and reserved regions; results are used to build a correct usable-page view.

### Milestone: PMM online (bitmap)

We have a working **bitmap physical memory manager (PMM)** suitable for QEMU `virt` bring-up:

- **Usable ranges are correct-by-construction for PMM**
  - Start from DTB `/memory` ranges (page-aligned)
  - Subtract DTB reserved ranges (page-aligned)
  - Subtract implicit reservations (page-aligned):
    - boot region `[lowest_ram_base, kernel_phys_base)`
    - kernel runtime footprint `[kernel_phys_base, kernel_runtime_end_pa)` (includes `.bss`)
    - DTB blob range
    - PMM metadata pages
  - Clamp output to the TTBR1 direct-map window (short-term correctness)
  - Output spans are sorted, merged, non-overlapping, and page-aligned.

- **PMM metadata placement (bootstrap, option B)**
  - A fixed number of pages immediately after the kernel runtime footprint are reserved for:
    - `struct pmm_state`
    - the bitmap
  - The bitmap is accessed via the high-half direct map:
    - `phys_to_virt(pa) = HH_PHYS_4000_BASE + (pa - RAM_BASE)`

- **Minimal PMM API (single-core)**
  - `pmm_init(const boot_info_t*)`
  - `pmm_alloc_page(uint64_t *out_pa)`
  - `pmm_alloc_pages(uint32_t count, uint64_t *out_pa)` (contiguous)
  - `pmm_free_page(uint64_t pa)`
  - Optional: `pmm_alloc_page_va(uint64_t *out_pa)` for direct-map VA

- **Debug validation**
  - With `KMAIN_DEBUG=1`, `kmain()` runs a quick allocation/free test and prints:
    - `PMM(free/total): free/total`
    - start/end counters plus intermediate cycles (single-page + contiguous allocations)

### Milestone: M6 — Interrupts + timer tick (GIC + ARM generic timer)

We now have a stable interrupt + timer baseline under QEMU `virt`:

- **Vector split: sync vs IRQ**
  - Synchronous exceptions stay on the existing “dump and park” path.
  - IRQs enter a distinct handler, dispatch, restore registers, and `eret` back to the interrupted context.

- **GICv2 bring-up**
  - `HAL/gicv2.*` initializes the distributor + CPU interface.
  - IRQ dispatch acknowledges the interrupt (IAR), routes to a registered handler, and EOIs it.

- **ARM Generic Timer (CNTV) periodic tick**
  - `HAL/timer_generic.*` programs CNTV for a periodic interrupt (default 100 Hz in `kmain()`).
  - The kernel maintains a `ticks` counter; `kmain()` prints a “Tick: N” line at a low rate (once per second) to avoid UART reentrancy/perf issues.

### Next steps

Recommended next milestones:

- **M7 — Cooperative threads**
  - context switch + thread structs
  - `thread_create(entry, arg)`, `yield()`, minimal round-robin run queue

- **M8 — Preemption (timer-driven)**
  - set a `need_resched` flag in the timer ISR
  - schedule-on-return / deferred reschedule path

- **M9 — Basic synchronization**
  - spinlocks, IRQ masking discipline, per-CPU scaffolding

## Build prerequisites

### macOS

Homebrew examples:

- LLVM + lld:
  - `brew install llvm lld`
- QEMU:
  - `brew install qemu`
- Python 3:
  - macOS typically ships a usable `python3`, or use `brew install python`
- Optional for debugging:
  - `brew install gdb` (or `gdb-multiarch` if you use it)

The build scripts default to Homebrew paths:
- LLVM tools: `/opt/homebrew/opt/llvm/bin`
- LLD: `/opt/homebrew/opt/lld/bin`

If you are on an Intel Mac or have different install paths, update `Kernel/Scripts/toolchain.env`.

### Linux

Debian/Ubuntu examples:

- `sudo apt-get update`
- `sudo apt-get install -y clang lld llvm make python3 qemu-system-aarch64 gdb-multiarch`

Notes:
- If your distro installs LLVM tools without versioned paths, you can typically use:
  - `LLVM_BIN=/usr/bin`
  - `LLD_BIN=/usr/bin`
  by editing `Kernel/Scripts/toolchain.env` (or by exporting `CC`, `LD`, etc. in your shell).

## Build

From the repo root:

```bash
./Kernel/Scripts/build.sh
```

This produces `build/kernel.img` and prints the computed kernel physical/virtual base addresses.

### Optional: enable a deliberate fault test

```bash
CAPAZ_FAULT_TEST=1 ./Kernel/Scripts/build.sh
```

## Run under QEMU

From the repo root:

```bash
QEMU_MACHINE="virt,gic-version=2" ./Kernel/Scripts/run-qemu.sh
```

Defaults (see `Kernel/Scripts/run-qemu.sh`):
- machine: `virt` (override to `virt,gic-version=2` for GICv2)
- CPU: `cortex-a72`
- memory: `512M`
- serial: `stdio`

### Manual run command (equivalent)

```bash
qemu-system-aarch64 \
  -machine virt,gic-version=2 \
  -cpu cortex-a72 -smp 1 \
  -m 128M \
  -nographic \
  -serial mon:stdio \
  -kernel build/kernel.img
```

### Run with a GDB stub

```bash
qemu-system-aarch64 \
  -machine virt,gic-version=2 \
  -cpu cortex-a72 -smp 1 \
  -m 128M \
  -nographic \
  -serial mon:stdio \
  -kernel build/kernel.img \
  -S -gdb tcp::1234
```

## Debug with GDB

In one terminal (QEMU, halted):

```bash
qemu-system-aarch64 \
  -machine virt,gic-version=2 \
  -cpu cortex-a72 -smp 1 \
  -m 128M \
  -nographic \
  -serial mon:stdio \
  -kernel build/kernel.img \
  -S -gdb tcp::1234
```

In another terminal:

```bash
gdb-multiarch build/kernel.elf
(gdb) target remote :1234
(gdb) b kmain
(gdb) c
```

Notes:
- If you want the earliest breakpoint, load `build/boot.elf` instead and break on `_start`.
- When QEMU is started with `-S`, the CPU is halted at reset; you must `continue` from the debugger for anything (including timer ticks) to run.

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
