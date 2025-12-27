#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* ---------------- Physical aliases for bootstrap (VMA==PA while MMU off) ---------------- */
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
extern uint8_t vectors[];
extern uint8_t vectors_phys[];

/* Link-time offset between PA load address and higher-half VMA. */
extern uint8_t __kern_offset[];
extern const uint64_t __boot_kern_offset_qword;

#ifdef __cplusplus
}
#endif
