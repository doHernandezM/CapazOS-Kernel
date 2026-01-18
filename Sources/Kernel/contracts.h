#pragma once
/*
 * contracts.h — Execution context contracts (Milestone M4.5)
 *
 * These contracts are always-on and intended to be small, explicit, and cheap.
 *
 * Rules:
 *  - IRQ context: cannot allocate, cannot block, cannot call into Core.
 *  - Thread context: may allocate and may call into Core.
 */

#include <stdbool.h>
#include "irq.h"
#include "panic.h"

static inline bool in_thread_context(void) { return !in_irq(); }

#define ASSERT_THREAD_CONTEXT() do { if (in_irq()) panic("ASSERT_THREAD_CONTEXT"); } while (0)
#define ASSERT_IRQ_CONTEXT()    do { if (!in_irq()) panic("ASSERT_IRQ_CONTEXT"); } while (0)

/* Alias for readability at call sites. */
#define ASSERT_NOT_IN_IRQ() ASSERT_THREAD_CONTEXT()

/*
 * Future-proofing for the Core boundary:
 * any ABI entrypoint callable by Core should assert thread context by default.
 */
#define CORE_ENTRY_GUARD() ASSERT_THREAD_CONTEXT()
