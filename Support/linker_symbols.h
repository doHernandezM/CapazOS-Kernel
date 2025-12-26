//
//  linker_symbols.h
//  Capaz
//
//  Linker-provided symbols for section boundaries and reserved regions.
//

#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot (low VA == PA) regions */
extern uint8_t __boot_text_start[];
extern uint8_t __boot_text_end[];
extern uint8_t __boot_rodata_start[];
extern uint8_t __boot_rodata_end[];
extern uint8_t __boot_data_start[];
extern uint8_t __boot_data_end[];

extern uint8_t __boot_stack_bottom[];
extern uint8_t __boot_stack_top[];

/* Physical vector base for early VBAR */
extern uint8_t vectors_phys[];

/* Reserved early MMU resources (page tables) */
extern uint8_t __pt_base[];
extern uint8_t __pt_end[];
extern uint8_t __pt_base_phys[];
extern uint8_t __pt_end_phys[];

/* Kernel stack (used after higher-half transition) */
extern uint8_t __stack_bottom[];
extern uint8_t __stack_top[];
extern uint8_t __stack_bottom_phys[];
extern uint8_t __stack_top_phys[];

/* Kernel image sections (VAs in higher-half) */
extern uint8_t __text_start[];
extern uint8_t __text_end[];
extern uint8_t __text_start_phys[];
extern uint8_t __text_end_phys[];

extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];
extern uint8_t __rodata_start_phys[];
extern uint8_t __rodata_end_phys[];

extern uint8_t __data_start[];
extern uint8_t __data_end[];
extern uint8_t __data_start_phys[];
extern uint8_t __data_end_phys[];

extern uint8_t __bss_start[];
extern uint8_t __bss_end[];
extern uint8_t __bss_start_phys[];
extern uint8_t __bss_end_phys[];

#ifdef __cplusplus
}
#endif
