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

/* Reserved early MMU resources */
extern uint8_t __pt_base[];
extern uint8_t __pt_end[];

/* Early kernel stack */
extern uint8_t __stack_bottom[];
extern uint8_t __stack_top[];

/* Kernel image sections */
extern uint8_t __text_start[];
extern uint8_t __text_end[];

extern uint8_t __rodata_start[];
extern uint8_t __rodata_end[];

extern uint8_t __data_start[];
extern uint8_t __data_end[];

extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

#ifdef __cplusplus
}
#endif
