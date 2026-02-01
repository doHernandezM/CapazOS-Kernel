# Core ↔ Kernel Boundary Rules

CapazOS ships as a **single project**: Kernel and Core are built and linked
into one system image. The previous “versioned ABI / services table” boundary
is removed.

We still keep a **policy boundary**.

## Roles

### Kernel provides mechanism
Kernel owns low-level mechanisms and hard invariants:

- IRQ/exception entry and return
- scheduler, threads, tasks
- memory management primitives (page tables, heap, slab) and context-safety rules
- capability tables, handle validity, rights enforcement
- IPC primitives and wait/wake mechanisms
- device drivers and DMA rules
- tracing and panic paths

### Core provides policy and services
Core owns system policy and most OS features:

- filesystem, object storage, sync/backup policy
- userland services (console server, registry, log service)
- security/permission policy decisions (what gets a cap and when)
- higher-level scheduling intent and resource contracts (policy layer on top of kernel mechanisms)

## Direction and dependency rules

- Core may call Kernel mechanisms through the internal API in `OS/Kern/Kernel/api/`.
- Kernel may not call into Core except through a very small boot/dispatch surface
  (Core entrypoints).
- No kernel code should depend on Core headers.
- Core code may include kernel “public” headers (`api/ks_types.h`,
  `api/core_kernel_api.h`). Core must not include kernel-private headers
  (e.g. `cap_table.h`, endpoint internals) unless explicitly marked Core-callable.

## Context and safety rules

### IRQ context

- Must not allocate.
- Must not block.
- Must not perform IPC send/recv.
- Must not call into Core.
- May only do bounded work and then defer to a thread/work-queue.

### Thread context

- May allocate and block, subject to subsystem-specific rules.
- May use IPC and capability operations.

## “Core-callable” API surface

All kernel functions callable by Core must be declared in:

- `OS/Kern/Kernel/api/ks_types.h` (shared types)
- `OS/Kern/Kernel/api/core_kernel_api.h` (Core-callable functions)

New Core-callable additions must:

- document allowed context (IRQ-safe or thread-only)
- document ownership and lifetime rules
- avoid leaking kernel internal structs into Core

## Data representation rules for shared messages

- Shared messages (IPC payloads, capability handles) must be POD with explicit integer widths.
- No varargs across the boundary.
- No implicit allocation in critical paths. If Core uses Swift, ensure shims and
  allocation paths are explicit.

## Policy wall hygiene

Even though Kernel and Core are in one binary:

- keep mechanism code in `Kern/Kernel` and policy/services in `Core/`
- avoid “policy creep” into kernel drivers and scheduler
- when in doubt, add a small mechanism hook in Kernel and implement the policy in Core
