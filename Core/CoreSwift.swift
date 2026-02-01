// CoreSwift.swift
//
// Swift entrypoint for Core (policy/services layer).
//
// When Swift is linked, core_main.c trampolines into core_main_swift.

@_silgen_name("cka_log_write")
private func cka_log_write(_ cstr: UnsafePointer<CChar>)

@_cdecl("core_main_swift")
public func core_main_swift() -> Int32 {
    "[core] Swift Core brought up (stub)\n".withCString { cka_log_write($0) }

    // TODO: Replace with real Core services bring-up:
    //  - capability-addressed console client/server
    //  - service registry
    //  - filesystem and other OS services
    return 0
}
