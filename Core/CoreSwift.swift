//
//  CoreSwift.swift
//  Core
//
//  Created by Cosas on 1/14/26.
//
//  This file defines the Swift entrypoint for the Core.  When the
//  kernel links this file into the kernel image (Option 2), the
//  exported function `core_main_swift` can be called from C to
//  transfer control into the Swift portion of Core.  At present
//  this function is a stub that returns immediately.


// Expose a C-callable entrypoint into the Swift Core.  This
// declaration uses @_cdecl to set the exported symbol name.  The
// kernel will call this function from core_main.c.  The stub
// implementation currently does nothing and simply returns 0.
@_cdecl("core_main_swift")
public func core_main_swift() -> Int32 {
    
    // TODO: Implement the Core kernel in Swift.  This stub is
    // intentionally minimal to allow linking and bootstrap without
    // requiring the Swift standard library.
    return 0
}

