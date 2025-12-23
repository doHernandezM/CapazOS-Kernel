//
//  KernelMain.swift
//  OSpost
//
//  Created by Cosas on 12/20/25.
//

@_silgen_name("uart_puts")
func uart_puts(_ s: UnsafePointer<CChar>)

@_cdecl("swift_kmain")
public func swift_kmain() {
    "======================\n".withCString { uart_puts($0) }
    "=OSpost: Hello World!=\n".withCString { uart_puts($0) }
    "======================\n".withCString { uart_puts($0) }
//    while true {}
}
