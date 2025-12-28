#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * When building the boot image (BOOT_STAGE), only declare the
 * low/identity-mapped symbols used by the bootstrap code. When
 * building the kernel image, declare the higher-half symbols and
 * physical aliases used by the kernel proper. This avoids pulling
 * unnecessary extern references into the wrong stage.
 */

#ifdef BOOT_STAGE

/* ---------------- Boot / identity (MMU off) ---------------- */
extern uint8_t __boot_text_start[];
extern uint8_t __boot_text_end[];

extern uint8_t __boot_rodata_start[];
extern uint8_t __boot_rodata_end[];

extern uint8_t __boot_data_start[];
extern uint8_t __boot_data_end[];

extern uint8_t __boot_bss_start[];
extern uint8_t __boot_bss_end[];

extern uint8_t __boot_stack_bottom[];
extern uint8_t __boot_stack_top[];

/* Physical aliases for bootstrap (VMA==PA while MMU off) */
extern uint8_t __text_start_phys[];
extern uint8_t __text_end_phys[];

extern uint8_t __rodata_start_phys[];
extern uint8_t __rodata_end_phys[];

extern uint8_t __data_start_phys[];
extern uint8_t __data_end_phys[];

extern uint8_t __bss_start_phys[];
extern uint8_t __bss_end_phys[];

extern uint8_t __pt_base_phys[];
extern uint8_t __pt_end_phys[];

extern uint8_t __stack_bottom_phys[];
extern uint8_t __stack_top_phys[];

/* Vector table symbols (both VMA and physical alias). */
extern uint8_t boot_vectors[];
extern uint8_t vectors_phys[];

/* Physical alias for crt0 (higher-half entry). The boot stage only
 * references crt0_phys when computing the kernel entry VA. */
extern uint8_t crt0_phys[];

#else /* !BOOT_STAGE */

/* ---------------- Higher-half kernel VMA symbols ---------------- */
extern uint8_t __text_start[];
extern uint8_t __text_end[];

extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];

extern uint8_t __data_start[];
extern uint8_t __data_end[];

extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

extern uint8_t __pt_base[];
extern uint8_t __pt_end[];

extern uint8_t __stack_bottom[];
extern uint8_t __stack_top[];

/* Vector table symbols (both VMA and physical alias). */
extern uint8_t vectors[];
extern uint8_t vectors_phys[];

/* Physical alias for crt0 (higher-half entry). */
extern uint8_t crt0_phys[];

#endif /* BOOT_STAGE */

/* Link-time offset between PA load address and higher-half VMA. This
 * exists in both stages so boot code can compute virtual addresses
 * from physical symbols when necessary. */
extern uint8_t __kern_offset[];
/*
 * The legacy build used to export a 64‑bit copy of __kern_offset
 * as __boot_kern_offset_qword.  The split boot/kernel build no
 * longer references this symbol, so it is intentionally omitted
 * to avoid unresolved externs during linking.
 */

#ifdef __cplusplus
}
#endif
