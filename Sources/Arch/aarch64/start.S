;
//  start.s
//  OSpost
//
//  Created by Cosas on 12/20/25.
//

.section .text._start, "ax"
.global _start
.type _start, %function

.extern __stack_top
.extern vectors

_start:
    // Set stack from linker-defined symbol
    ldr x0, =__stack_top
    mov sp, x0

    // Install EL1 vector table
    ldr x0, =vectors
    msr vbar_el1, x0
    isb

    bl  crt0

1:  wfe
    b   1b
