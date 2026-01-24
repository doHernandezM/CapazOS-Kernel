# Core ↔ Kernel Boundary Rules (Option A: Stable C ABI)

This document defines the **only** rules for data and control flow across the Core/Kernel boundary.

## Boundary headers

Canonical include root: `Kernel/Sources/ABI/`

* `core_kernel_abi.h` — umbrella include and ABI version
* `kernel_services_v1.h` — Kernel → Core services table (vtable)
* `core_entrypoints.h` — Core entrypoints callable by Kernel

**Rule:** there must be exactly one include path for boundary headers: `-I Kernel/Sources/ABI`.

## Allowed types across the boundary

* **POD only**: integers, enums with fixed underlying types, pointers, plain C structs.
* No Swift-only types (String, Array, class references, generics, closures) across the boundary.
* No `...` varargs.
* No packing changes without a version bump.

## Versioning

* `CAPAZ_CORE_KERNEL_ABI_VERSION` bumps on *any* breaking change to any boundary header.
* Service table version uses `CAPAZ_KERNEL_SERVICES_V1_MAJOR/MINOR`.

## Context rules

* **IRQ context**: must not block; must not allocate; must not call into Core unless an API explicitly says it is IRQ-safe.
* **Thread context**: may block and may allocate (subject to allocator policy).
* Kernel must provide a way for Core to query context (`in_irq()` or equivalent) if Core needs to gate behavior.

## Allocation rules

* Core must treat *all* allocation as a policy decision (and therefore context-sensitive).
* The only allocations Core may perform are through explicit kernel APIs (e.g. `services->alloc`) that document context safety.
* No implicit allocations in IRQ paths (including Swift ARC traffic or metadata work) unless proven safe.

## Error handling

* Kernel provides `panic()` and `log()`.
* Boundary functions return integer status codes where recovery is possible.
* “Impossible” states may call `panic()` (never return).
