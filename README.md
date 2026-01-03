# CapazOS Kernel (AArch64)

## Concept

CapazOS is an educational, freestanding AArch64 kernel that boots under **QEMU `virt`** and is being built up in small milestones:
- bring-up (MMU, vectors, UART)
- hardware discovery via **Device Tree Blob (DTB / FDT)**
- physical memory management (PMM) and allocation
- interrupts (GIC), timers, scheduler, userspace, etc.

The project uses a **higher-half direct map** so physical RAM at `0x4000_0000+` is also visible at:
`0xFFFF8000_4000_0000 + (pa - 0x4000_0000)` (exact base is defined in the boot/kernel code).

## Kernel overview

Current components (high level):

- **Boot stage (`start.S`)**
  - sets up an initial stack
  - installs temporary exception vectors
  - builds minimal translation tables
  - enables the MMU at EL1
  - constructs a `boot_info` structure
  - jumps to the kernel entry (using the kernel image header’s entry offset)
  - passes a DTB pointer via `boot_info` (DTB is expected to be in RAM and direct-mapped)

- **C runtime trampoline (`kcrt0.c`)**
  - is the first C-visible entry point
  - canonicalizes the `boot_info` pointer (48-bit VA sign extension)
  - moves `SP` into the higher-half direct map (so disabling TTBR0 won’t break stack use)
  - tail-calls the “real” C entry, then into `kmain()`

- **Kernel main (`kmain.c`)**
  - initializes the kernel MMU tables (TTBR1) and disables TTBR0 for kernel-only VA space
  - initializes UART (PL011) and prints a banner
  - (in progress) parses DTB to derive platform data for other subsystems

- **Exception vectors**
  - boot vectors exist for early bring-up
  - kernel vectors are installed once the kernel is running, with basic fault reporting hooks

## Current status

As of **Boot/Kernel 0.0.3** (based on the latest debug output in this repo’s history):

- The system reliably reaches the kernel and prints:
  - `Boot: 0.0.3`
  - `Kernel: 0.0.3`
- `boot_info` contains:
  - kernel physical base + image size + entry offset
  - DTB virtual address (already in the higher-half direct map) + DTB size
- DTB parsing currently extracts:
  - **memory ranges** (example observed under QEMU: `0x4000_0000` size `0x0800_0000` = 128 MiB)
  - reserved ranges are not yet discovered (DTB often reports none unless configured)
  - UART cannot yet be derived from `/chosen/stdout-path` (fallback UART base is used)

### What is “next”

Immediate next work (no allocator yet):
1. Use DTB `/chosen` + `/aliases` to resolve `stdout-path` to the UART node and parse its `reg` for the MMIO base.
2. Collect reserved ranges from:
   - DTB **memreserve** header entries
   - `/reserved-memory` subnodes
   - implicit reservations: kernel image, DTB region, and boot/kernel page tables
3. Derive **usable RAM ranges** = memory ranges minus reserved ranges.
4. Make MMU RAM mapping iterate DTB-derived memory ranges rather than mapping a fixed window.

Once usable ranges exist, it becomes reasonable to start discussing:
- an early **physical page allocator (PMM)** built from usable ranges
- a simple bootstrap allocator for early kernel allocations (before a full VM/heap).

## License

See `LICENSE` (BSD 2-Clause).
