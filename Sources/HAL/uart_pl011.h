//
//  uart_pl011.h
//  OSpost
//
//  Created by Cosas on 12/20/25.
//

#pragma once
#include <stdint.h>

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char* s);
