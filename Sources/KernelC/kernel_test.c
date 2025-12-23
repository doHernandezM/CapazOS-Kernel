//
//  kernel_test.c
//  OSpost
//
//  Phase 2 validation tests.
//  These tests intentionally fault the kernel to prove protections.
//  Enable with -DKTEST_ENABLE=1 and expect the kernel to halt after printing
//  the exception diagnostics.
//

#include "kernel_test.h"
#include "uart_pl011.h"
#include "linker_symbols.h"

#if KTEST_ENABLE

#include "uart_pl011.h"
#include "sysreg.h"     // read_sctlr_el1()
#include <stddef.h>
#include "cpu.h"

static uint32_t g_flags = KTEST_DEFAULT_FLAGS;

typedef enum { KTR_PASS = 0, KTR_FAIL = 1, KTR_SKIP = 2 } ktest_result_t;

typedef ktest_result_t (*ktest_fn_t)(void);

typedef struct {
  const char* name;
  ktest_stage_t stage;
  uint32_t feature_mask;
  ktest_fn_t fn;
} ktest_entry_t;

static void put_hex_u64(uint64_t v) {
  static const char* kHex = "0123456789abcdef";
  uart_puts("0x");
  for (int i = 60; i >= 0; i -= 4) uart_putc(kHex[(v >> (uint64_t)i) & 0xFULL]);
}

static void put_dec_u32(uint32_t v) {
  char buf[11];
  int i = 10;
  buf[i--] = '\0';
  if (v == 0) { uart_putc('0'); return; }
  while (v && i >= 0) { buf[i--] = (char)('0' + (v % 10)); v /= 10; }
  uart_puts(&buf[i + 1]);
}

static void log_result(const char* name, ktest_result_t r) {
  uart_puts("  [");
  uart_puts(r == KTR_PASS ? "PASS" : (r == KTR_SKIP ? "SKIP" : "FAIL"));
  uart_puts("] ");
  uart_puts(name);
  uart_puts("\n");
}

/* -------------------------------------------------------------------------- */
/* Exception observation hook (called from exceptions.c if present)            */
/* -------------------------------------------------------------------------- */

static volatile uint32_t g_last_ec = 0;
static volatile uint32_t g_last_iss = 0;
static volatile uint64_t g_last_type = 0;
static volatile uint64_t g_last_origin = 0;
static volatile uint32_t g_exc_seen = 0;

// This symbol is called by exceptions.c via a weak reference.
void ktest_exception_observed(uint64_t type, uint64_t origin, uint32_t ec, uint32_t iss) {
  g_last_type = type;
  g_last_origin = origin;
  g_last_ec = ec;
  g_last_iss = iss;
  g_exc_seen++;
}

static void exc_reset_seen(void) {
  g_exc_seen = 0;
  g_last_ec = 0;
  g_last_iss = 0;
  g_last_type = 0;
  g_last_origin = 0;
}

/* -------------------------------------------------------------------------- */
/* Tests                                                                       */
/* -------------------------------------------------------------------------- */

static ktest_result_t test_mmu_enabled_bit(void) {
  uint64_t sctlr = read_sctlr_el1();
  if (sctlr & (1ull << 0)) return KTR_PASS;
  uart_puts("    SCTLR_EL1.M expected 1, got "); put_hex_u64(sctlr); uart_puts("\n");
  return KTR_FAIL;
}

static ktest_result_t test_cache_bits(void) {
  uint64_t sctlr = read_sctlr_el1();
  const uint64_t C = (1ull << 2);
  const uint64_t I = (1ull << 12);
  if ((sctlr & C) && (sctlr & I)) return KTR_PASS;
  uart_puts("    SCTLR_EL1.C/I expected 1/1, got "); put_hex_u64(sctlr); uart_puts("\n");
  return KTR_FAIL;
}

// BRK resume + register preservation (Phase 1 requirement)
static ktest_result_t test_brk_resume_preserves_x1_x2(void) {
  exc_reset_seen();

  const uint64_t sent1 = 0x1122334455667788ull;
  const uint64_t sent2 = 0x99aabbccddeeff00ull;
  uint64_t out1 = 0, out2 = 0;

  __asm__ volatile(
    "mov x1, %2\n"
    "mov x2, %3\n"
    "brk #0\n"
    "mov %0, x1\n"
    "mov %1, x2\n"
    : "=r"(out1), "=r"(out2)
    : "r"(sent1), "r"(sent2)
    : "x1", "x2", "memory"
  );

  if (out1 != sent1 || out2 != sent2) {
    uart_puts("    x1/x2 corrupted across BRK: out1="); put_hex_u64(out1);
    uart_puts(" out2="); put_hex_u64(out2); uart_puts("\n");
    return KTR_FAIL;
  }

  // Optional: confirm exception router saw BRK (EC=0x3C)
  if (g_exc_seen == 0 || g_last_ec != 0x3C) {
    uart_puts("    warning: BRK hook not observed (seen="); put_dec_u32(g_exc_seen);
    uart_puts(" ec="); put_hex_u64((uint64_t)g_last_ec); uart_puts(")\n");
    // Not a failure; register preservation is the real requirement.
  }

  return KTR_PASS;
}

// SVC routing (non-destructive). Verifies we resume and the trap path is wired.
// Requires exceptions.c to route SVC as RESUME (your Phase 1 plan).
static ktest_result_t test_svc_routes_and_resumes(void) {
  exc_reset_seen();

  __asm__ volatile("svc #0" ::: "memory");

  // EC for SVC AArch64 is 0x15.
  if (g_exc_seen == 0) {
    uart_puts("    expected SVC to be observed, saw none\n");
    return KTR_FAIL;
  }
  if (g_last_ec != 0x15) {
    uart_puts("    expected EC=0x15 (SVC), got "); put_hex_u64((uint64_t)g_last_ec); uart_puts("\n");
    return KTR_FAIL;
  }
  return KTR_PASS;
}

// --- IRQ/Timer sysregs -------------------------------------------------------

static inline uint64_t read_cntfrq_el0(void) {
  uint64_t v;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
  return v;
}

static inline void write_cntv_tval_el0(uint32_t v) {
  __asm__ volatile("msr cntv_tval_el0, %0" :: "r"(v));
}

static inline void write_cntv_ctl_el0(uint32_t v) {
  __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(v));
}

static inline void irq_unmask(void) {
  // Clear DAIF.I (IRQ mask). #2 corresponds to I.
  __asm__ volatile("msr daifclr, #2" ::: "memory");
}

static inline void irq_mask(void) {
  // Set DAIF.I (IRQ mask)
  __asm__ volatile("msr daifset, #2" ::: "memory");
}


static ktest_result_t test_irq_virtual_timer_fires(void) {
  exc_reset_seen();

  // Initialize minimal GICv2 path (QEMU virt -machine virt commonly uses GICv2 unless configured otherwise)
  gicv2_init_minimal_for_timer();

  // Program EL1 virtual timer: enable, unmask IRQs, set a short timeout.
  // CNTV_CTL: bit0=ENABLE, bit1=IMASK (0=unmasked), bit2=ISTATUS (RO)
  write_cntv_ctl_el0(0u); // disable first (and unmask)
  
  uint64_t frq = read_cntfrq_el0();
  if (frq == 0) {
    uart_puts("    cntfrq_el0 returned 0\n");
    return KTR_FAIL;
  }

  // ~1ms worth of ticks (clamp to a minimum)
  uint32_t ticks = (uint32_t)(frq / 1000u);
  if (ticks < 100u) ticks = 100u;

  write_cntv_tval_el0(ticks);
  write_cntv_ctl_el0(1u); // ENABLE=1, IMASK=0

  irq_unmask();

  // Wait for an IRQ to be observed. We expect:
  //   type == IRQ (1)
  // For EC/ISS, IRQ entries don't have meaningful EC like SYNC traps; they should be 0.
  // We'll validate primarily on 'type'.
  const uint32_t spin_max = 500000u;
  uint32_t spin = 0;
  while (g_exc_seen == 0 && spin++ < spin_max) {
    cpu_wfe();
  }

  irq_mask();
  write_cntv_ctl_el0(0u); // disable timer

  if (g_exc_seen == 0) {
    uart_puts("    expected IRQ via virtual timer; none observed.\n");
    uart_puts("    note: if QEMU is using GICv3, your GICv2 init won't work.\n");
    return KTR_FAIL;
  }

  if (g_last_type != 1 /* EXC_TYPE_IRQ */) {
    uart_puts("    expected exception type IRQ(1), got "); put_hex_u64(g_last_type); uart_puts("\n");
    uart_puts("    last EC="); put_hex_u64((uint64_t)g_last_ec);
    uart_puts(" ISS="); put_hex_u64((uint64_t)g_last_iss); uart_puts("\n");
    return KTR_FAIL;
  }

  return KTR_PASS;
}

/* -------------------------------------------------------------------------- */
/* Registry                                                                    */
/* -------------------------------------------------------------------------- */

static const ktest_entry_t kTests[] = {
  // MMU stage tests
  { "MMU enabled bit set (SCTLR_EL1.M)",         KTEST_STAGE_POST_MMU,   KTEST_F_MMU,        test_mmu_enabled_bit },
  // Cache stage tests
  { "Caches enabled bits set (SCTLR_EL1.C/I)",   KTEST_STAGE_POST_CACHE, KTEST_F_CACHE,      test_cache_bits },
  // Exception tests (safe anywhere after vectors installed; run from KMAIN by default)
  { "BRK resumes and preserves x1/x2",           KTEST_STAGE_KMAIN,      KTEST_F_EXCEPTIONS, test_brk_resume_preserves_x1_x2 },
  { "SVC routes and resumes",                    KTEST_STAGE_KMAIN,      KTEST_F_EXCEPTIONS, test_svc_routes_and_resumes },
  { "IRQ: virtual timer fires (CNTV + GIC)", KTEST_STAGE_KMAIN, KTEST_F_IRQ, test_irq_virtual_timer_fires },
};

void ktest_set_flags(uint32_t flags) { g_flags = flags; }

void ktest_run_stage(ktest_stage_t stage) {
  uart_puts("\n[ktest] stage=");
  put_dec_u32((uint32_t)stage);
  uart_puts(" flags=");
  put_hex_u64((uint64_t)g_flags);
  uart_puts("\n");

  uint32_t pass = 0, fail = 0, skip = 0;

  for (size_t i = 0; i < (sizeof(kTests) / sizeof(kTests[0])); i++) {
    const ktest_entry_t* t = &kTests[i];
    if (t->stage != stage) continue;

    if ((g_flags & t->feature_mask) == 0) {
      log_result(t->name, KTR_SKIP);
      skip++;
      continue;
    }

    ktest_result_t r = t->fn();
    log_result(t->name, r);
    if (r == KTR_PASS) pass++;
    else if (r == KTR_FAIL) fail++;
    else skip++;
  }

  uart_puts("[ktest] summary: pass="); put_dec_u32(pass);
  uart_puts(" fail="); put_dec_u32(fail);
  uart_puts(" skip="); put_dec_u32(skip);
  uart_puts("\n");
}


static void banner(const char *s) {
    uart_puts("\n[KTEST] ");
    uart_puts(s);
    uart_puts("\n");
}

static void test_write_text(void) {
    banner("writing to .text (should fault)");
    volatile uint64_t *p = (volatile uint64_t *)(uintptr_t)__text_start;
    *p = 0x1122334455667788ull;
}

static void test_exec_from_rw(void) {
    banner("execute from RW page (should fault)");
    /* 'brk #0' (0xD4200000), then 'ret' (0xD65F03C0) */
    static uint32_t code[2] __attribute__((aligned(8)));
    code[0] = 0xD4200000u;
    code[1] = 0xD65F03C0u;
    void (*fn)(void) = (void (*)(void))(uintptr_t)code;
    fn();
}

static void test_null_deref(void) {
    banner("null deref (should fault)");
    volatile uint64_t *p = (volatile uint64_t *)0x0;
    (void)*p;
}

void kernel_test_run(void) {
    uart_puts("\n====================\n");
    uart_puts("[KTEST] enabled\n");
    uart_puts("====================\n");

    /* Run one at a time; kernel will halt on first fault. */
    test_null_deref();
    /* test_write_text(); */
    /* test_exec_from_rw(); */

    uart_puts("[KTEST] completed (unexpected)\n");
}

#endif // KTEST_ENABLE
 
