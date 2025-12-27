//
//  sysreg.h
//  Capaz
//
//  System register helpers.
//  Phase 3: TTBR1 + TTBR0, PAR_EL1 translation probes, basic TLB ops.
//

#pragma once
#include <stdint.h>

#define SYSREG_INLINE static inline __attribute__((always_inline))

SYSREG_INLINE void isb(void)        { __asm__ volatile("isb" ::: "memory"); }
SYSREG_INLINE void dsb_ish(void)    { __asm__ volatile("dsb ish" ::: "memory"); }
SYSREG_INLINE void dsb_ishst(void)  { __asm__ volatile("dsb ishst" ::: "memory"); }

SYSREG_INLINE uint64_t read_sctlr_el1(void) {
    uint64_t v; __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}
SYSREG_INLINE void write_sctlr_el1(uint64_t v) {
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(v));
}

SYSREG_INLINE uint64_t read_tcr_el1(void) {
    uint64_t v; __asm__ volatile("mrs %0, tcr_el1" : "=r"(v));
    return v;
}
SYSREG_INLINE void write_tcr_el1(uint64_t v) {
    __asm__ volatile("msr tcr_el1, %0" :: "r"(v));
}

SYSREG_INLINE void write_mair_el1(uint64_t v) {
    __asm__ volatile("msr mair_el1, %0" :: "r"(v));
}

SYSREG_INLINE uint64_t read_ttbr0_el1(void) {
    uint64_t v; __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(v));
    return v;
}
SYSREG_INLINE void write_ttbr0_el1(uint64_t v) {
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v));
}
SYSREG_INLINE void write_ttbr1_el1(uint64_t v) {
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(v));
}

/* Translation result register for AT instructions */
SYSREG_INLINE uint64_t read_par_el1(void) {
    uint64_t v; __asm__ volatile("mrs %0, par_el1" : "=r"(v));
    return v;
}

/* Address translation probes (do not fault; results appear in PAR_EL1) */
SYSREG_INLINE void at_s1e0r(uint64_t va) {
    __asm__ volatile("at s1e0r, %0" :: "r"(va));
    isb();
}
SYSREG_INLINE void at_s1e0w(uint64_t va) {
    __asm__ volatile("at s1e0w, %0" :: "r"(va));
    isb();
}

SYSREG_INLINE void tlbi_vmalle1(void) {
    __asm__ volatile("tlbi vmalle1" ::: "memory");
}
/* Invalidate all TLB entries for stage-1 EL0&EL1 on the inner-shareable domain */
SYSREG_INLINE void invalidate_tlb_all_el1(void) {
    dsb_ishst();
    tlbi_vmalle1();
    dsb_ish();
    isb();
}

/* ASID-scoped invalidation (optional; safe to fall back to VMALLE1) */
SYSREG_INLINE void tlbi_aside1is(uint16_t asid) {
    // TLBI ASIDE1IS, Xt: invalidates entries matching ASID in current translation regime.
    // Encoding uses Xt = asid in bits[15:0] of operand.
    uint64_t v = (uint64_t)asid;
    dsb_ishst();
    __asm__ volatile("tlbi aside1is, %0" :: "r"(v) : "memory");
    dsb_ish();
    isb();
}
