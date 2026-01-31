#include "core_sections.h"
#include <stdint.h>

/* Bring-up validation: ensure Core produces content in each Core section. */
static const char kCoreConst[] = "core-smoke";      /* .rodata.core */
int32_t core_data_value = 123;                      /* .data.core */
int32_t core_bss_value;                             /* .bss.core */

__attribute__((used))
int32_t core_smoke_add(int32_t a, int32_t b)
{
    /* Reference each storage class so the linker keeps them. */
    return a + b + core_data_value + core_bss_value + (int32_t)kCoreConst[0];
}
