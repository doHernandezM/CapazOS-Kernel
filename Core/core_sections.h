#ifndef CORE_SECTIONS_H
#define CORE_SECTIONS_H

/*
 * Place all code/data in this translation unit into Core sections.
 * This is used during bring-up to ensure Core objects land in:
 *   .text.core / .rodata.core / .data.core / .bss.core
 */
#pragma clang section text=".text.core" rodata=".rodata.core" data=".data.core" bss=".bss.core"

#endif /* CORE_SECTIONS_H */
