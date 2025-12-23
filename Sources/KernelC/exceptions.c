// exceptions.c
// OSpost
//
// Phase 1: AArch64 EL1 exception dispatch as a trap policy engine.
// Goals:
//  - faithful frame snapshot (handled in vectors.S)
//  - decode ESR/EC/ISS and route to typed handlers
//  - policy outcomes: resume / kill-current / panic
//
// Current limitation:
//  - EXC_ACTION_KILL is defined but treated as PANIC in vectors.S until a task model exists.

#include "exceptions.h"
#include "uart_pl011.h"

// Optional kernel-test hook (resolved only when kernel_test.c is linked in).
extern void ktest_exception_observed(uint64_t type, uint64_t origin, uint32_t ec, uint32_t iss) __attribute__((weak));


static void uart_put_hex_u64(uint64_t v) {
    static const char* kHex = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        uart_putc(kHex[(v >> (uint64_t)i) & 0xFULL]);
    }
}

static void uart_put_hex_u32(uint32_t v) {
    static const char* kHex = "0123456789abcdef";
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uart_putc(kHex[(v >> (uint32_t)i) & 0xFULL]);
    }
}

static const char* type_str(uint64_t type) {
    switch (type) {
        case EXC_TYPE_SYNC:   return "SYNC";
        case EXC_TYPE_IRQ:    return "IRQ";
        case EXC_TYPE_FIQ:    return "FIQ";
        case EXC_TYPE_SERROR: return "SError";
        default:              return "UNKNOWN";
    }
}

static const char* origin_str(uint64_t origin) {
    switch (origin) {
        case EXC_ORIGIN_CUR_SP0: return "CURRENT_EL SP0";
        case EXC_ORIGIN_CUR_SPX: return "CURRENT_EL SPx";
        case EXC_ORIGIN_LOW_A64: return "LOWER_EL AArch64";
        case EXC_ORIGIN_LOW_A32: return "LOWER_EL AArch32";
        default:                 return "UNKNOWN";
    }
}

static const char* ec_str(uint32_t ec) {
    switch (ec) {
        case 0x00: return "Unknown reason";
        case 0x15: return "SVC (AArch64)";
        case 0x20: return "Instr Abort (lower EL)";
        case 0x21: return "Instr Abort (same EL)";
        case 0x24: return "Data Abort (lower EL)";
        case 0x25: return "Data Abort (same EL)";
        case 0x2F: return "SError interrupt";
        case 0x3C: return "BRK (AArch64)";
        default:   return "Other";
    }
}

static int is_lower_el(uint64_t origin) {
    return origin == EXC_ORIGIN_LOW_A64 || origin == EXC_ORIGIN_LOW_A32;
}

// ---- Abort ISS decoding (minimal but stable) ----
// For Data Abort: ISS[5:0]=DFSC, ISS[6]=WnR
static void print_data_abort_iss(uint32_t iss) {
    uint32_t dfsc = iss & 0x3Fu;
    uint32_t wnr  = (iss >> 6) & 0x1u;

    uart_puts("DataAbort: DFSC="); uart_put_hex_u32(dfsc);
    uart_puts(" WnR="); uart_puts(wnr ? "W" : "R");
    uart_puts("\n");
}

// For Instruction Abort: ISS[5:0]=IFSC
static void print_instr_abort_iss(uint32_t iss) {
    uint32_t ifsc = iss & 0x3Fu;
    uart_puts("InstrAbort: IFSC="); uart_put_hex_u32(ifsc);
    uart_puts("\n");
}

// ---- Typed handlers (Phase 1) ----
static exc_action_t brk_handler(exception_frame_t* f, uint32_t iss) {
    (void)iss;
    uart_puts("*** BRK: RESUMING (ELR += 4) ***\n");
    f->elr_el1 += 4;
    return EXC_ACTION_RESUME;
}

static exc_action_t svc_handler(exception_frame_t* f, uint32_t iss) {
    (void)iss;
    (void)f;
    uart_puts("*** SVC: RESUMING (stub) ***\n");
    return EXC_ACTION_RESUME;
}

static exc_action_t irq_handler(exception_frame_t* f) {
    (void)f;
    uart_puts("*** IRQ: RESUMING (stub) ***\n");
    return EXC_ACTION_RESUME;
}

static exc_action_t data_abort_handler(uint64_t origin, exception_frame_t* f, uint32_t ec, uint32_t iss) {
    (void)f; (void)ec;
    print_data_abort_iss(iss);

    if (is_lower_el(origin)) {
        uart_puts("*** Data abort from lower EL: KILL_CURRENT (placeholder) ***\n");
        return EXC_ACTION_KILL;
    }

    uart_puts("*** Data abort in kernel: PANIC ***\n");
    return EXC_ACTION_PANIC;
}

static exc_action_t instr_abort_handler(uint64_t origin, exception_frame_t* f, uint32_t ec, uint32_t iss) {
    (void)f; (void)ec;
    print_instr_abort_iss(iss);

    if (is_lower_el(origin)) {
        uart_puts("*** Instr abort from lower EL: KILL_CURRENT (placeholder) ***\n");
        return EXC_ACTION_KILL;
    }

    uart_puts("*** Instr abort in kernel: PANIC ***\n");
    return EXC_ACTION_PANIC;
}

static exc_action_t default_handler(uint64_t type, uint64_t origin, exception_frame_t* f, uint32_t ec, uint32_t iss) {
    (void)type; (void)origin; (void)f; (void)ec; (void)iss;
    uart_puts("*** Unhandled exception: PANIC ***\n");
    return EXC_ACTION_PANIC;
}

// ---- Dispatcher (Phase 1) ----
exc_action_t exception_dispatch(exception_frame_t* f, uint64_t type, uint64_t origin) {
    uart_puts("\n\n====================\n");
    uart_puts("*** EXCEPTION ***\n");
    uart_puts("Type:   "); uart_puts(type_str(type)); uart_puts("\n");
    uart_puts("Origin: "); uart_puts(origin_str(origin)); uart_puts("\n");

    uart_puts("ELR_EL1:  ");  uart_put_hex_u64(f->elr_el1);  uart_puts("\n");
    uart_puts("ESR_EL1:  ");  uart_put_hex_u64(f->esr_el1);  uart_puts("\n");
    uart_puts("SPSR_EL1: ");  uart_put_hex_u64(f->spsr_el1); uart_puts("\n");
    uart_puts("FAR_EL1:  ");  uart_put_hex_u64(f->far_el1);  uart_puts("\n");

    uint32_t ec  = (uint32_t)((f->esr_el1 >> 26) & 0x3Fu);
    uint32_t iss = (uint32_t)(f->esr_el1 & 0x01FFFFFFu);

    if (ktest_exception_observed) {
        ktest_exception_observed(type, origin, ec, iss);
    }
    
    uart_puts("EC:  "); uart_put_hex_u32(ec);  uart_puts(" ("); uart_puts(ec_str(ec)); uart_puts(")\n");
    uart_puts("ISS: "); uart_put_hex_u32(iss); uart_puts("\n");

    uart_puts("x0: "); uart_put_hex_u64(f->x[0]); uart_puts("  ");
    uart_puts("x1: "); uart_put_hex_u64(f->x[1]); uart_puts("  ");
    uart_puts("x2: "); uart_put_hex_u64(f->x[2]); uart_puts("\n");

    exc_action_t act = EXC_ACTION_PANIC;

    if (type == EXC_TYPE_IRQ) {
        act = irq_handler(f);
    } else if (type == EXC_TYPE_SYNC) {
        switch (ec) {
            case 0x3C: act = brk_handler(f, iss); break;
            case 0x15: act = svc_handler(f, iss); break;
            case 0x24:
            case 0x25:
                act = data_abort_handler(origin, f, ec, iss);
                break;
            case 0x20:
            case 0x21:
                act = instr_abort_handler(origin, f, ec, iss);
                break;
            default:
                act = default_handler(type, origin, f, ec, iss);
                break;
        }
    } else {
        act = default_handler(type, origin, f, ec, iss);
    }

    if (act == EXC_ACTION_RESUME) {
        uart_puts("====================\n");
        return act;
    }

    if (act == EXC_ACTION_KILL) {
        uart_puts("*** NOTE: KILL_CURRENT not implemented yet; treating as PANIC ***\n");
        uart_puts("====================\n");
        return act; // vectors.S treats this as panic for now
    }

    uart_puts("*** PANIC: HALTING ***\n");
    uart_puts("====================\n");
    return act;
}
