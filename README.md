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

As of **Boot/Kernel 0.0.4**:

- Stable bring-up to the kernel stage (MMU enabled, vectors installed).
- Higher-half direct map is active early; TTBR0 is default-deny (disabled) in kernel runtime.
- Kernel C runtime now zeroes `.bss` deterministically before `kmain()`.
- DTB parsing provides `/memory` ranges and reserved regions; results are used to build a correct usable-page view.

### Milestone: PMM online (bitmap)

We now have a working **bitmap physical memory manager (PMM)** suitable for QEMU `virt` bring-up:

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

### Next steps

- Integrate PMM into page table growth (keep static `.bss` base tables; allocate any additional tables from PMM).
- Replace/retire any remaining early bump allocation once PMM is initialized.
- Layer a kernel heap on PMM:
  - Phase A: page-granularity allocations
  - Phase B: small-object allocator (slab / bucketed freelists) backed by PMM pages
- Add locking (spinlock) around PMM for SMP, once secondary cores are enabled.

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
