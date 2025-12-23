//
//  mem.h
//  OSpost
//
//  Created by Cosas on 12/21/25.
//

#pragma once
#ifndef MEM_H
#define MEM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* memset(void* dst, int c, size_t n);
void* memcpy(void* dst, const void* src, size_t n);
void* memmove(void* dst, const void* src, size_t n);

#ifdef __cplusplus
}
#endif

#endif // !MEM_H
