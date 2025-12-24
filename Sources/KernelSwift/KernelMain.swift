//
//  KernelMain.swift
//  OSpost
//
//  Created by Cosas on 12/20/25.
//

@_silgen_name("uart_puts")
func print(_ s: UnsafePointer<CChar>)

@_silgen_name("uart_putn")
func printLine(_ s: UnsafePointer<CChar>)

@_cdecl("swift_kmain")
public func swift_kmain() {
    "\n======================\n".withCString { print($0) }
    "= CapazOS: 0.0.1     =\n".withCString { print($0) }
    "======================\n".withCString { print($0) }
//    while true {}
}
