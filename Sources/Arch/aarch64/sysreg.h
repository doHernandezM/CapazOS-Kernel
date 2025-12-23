//
//  sysreg.h
//  OSpost
//
//  Created by Cosas on 12/21/25.
//

#pragma once
#include <stdint.h>

static inline void isb(void)  { __asm__ volatile("isb" ::: "memory"); }
static inline void dsb_ish(void) { __asm__ volatile("dsb ish" ::: "memory"); }
static inline void dsb_ishst(void) { __asm__ volatile("dsb ishst" ::: "memory"); }

static inline uint64_t read_sctlr_el1(void) {
    uint64_t v; __asm__ volatile("mrs %0, sctlr_el1" : "=r"(v));
    return v;
}
static inline void write_sctlr_el1(uint64_t v) {
    __asm__ volatile("msr sctlr_el1, %0" :: "r"(v));
}

static inline void write_ttbr0_el1(uint64_t v) {
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(v));
}

static inline void write_tcr_el1(uint64_t v) {
    __asm__ volatile("msr tcr_el1, %0" :: "r"(v));
}

static inline void write_mair_el1(uint64_t v) {
    __asm__ volatile("msr mair_el1, %0" :: "r"(v));
}

static inline void tlbi_vmalle1(void) {
    __asm__ volatile("tlbi vmalle1" ::: "memory");
}

static inline void invalidate_tlb_all_el1(void) {
    dsb_ishst();
    tlbi_vmalle1();
    dsb_ish();
    isb();
}
