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
// kernel will call this function from core_main.c.

// ---- C interop shims ----

// core_boot_if_t layout (must match Kern/Kernel/api/core_boot_if.h).
struct core_boot_if_t {
    var version: UInt32
    var _pad0: UInt32
    var core_caps: UnsafeMutableRawPointer?
    var core_task: UnsafeMutableRawPointer?
    var console_ep: UInt64
    var uart_cmd_ep: UInt64
    var uart_evt_ep: UInt64
}

@_silgen_name("core_boot_if")
func core_boot_if() -> UnsafePointer<core_boot_if_t>?

@_silgen_name("cka_ipc_send")
func cka_ipc_send(_ endpoint: UInt64, _ msg: UnsafeRawPointer) -> Int32

@_silgen_name("cka_ipc_recv")
func cka_ipc_recv(_ endpoint: UInt64, _ msg: UnsafeMutableRawPointer) -> Int32

private let KS_IPC_MSG_MAX: Int = 128
private let KS_STATUS_OK: Int32 = 0

private let UART_TAG_WRITE: UInt32 = 1
private let UART_TAG_RX_EVENT: UInt32 = 2

struct UARTClient {
    let cmdEp: UInt64
    let evtEp: UInt64

    func write(_ bytes: [UInt8]) {
        if cmdEp == 0 || bytes.isEmpty {
            return
        }
        let count = min(bytes.count, KS_IPC_MSG_MAX)
        let msgSize = 8 + KS_IPC_MSG_MAX
        let buf = UnsafeMutableRawPointer.allocate(byteCount: msgSize, alignment: 8)
        defer { buf.deallocate() }

        buf.storeBytes(of: UART_TAG_WRITE, as: UInt32.self)
        buf.storeBytes(of: UInt32(count), toByteOffset: 4, as: UInt32.self)
        bytes.withUnsafeBytes { src in
            if let base = src.baseAddress {
                buf.advanced(by: 8).copyMemory(from: base, byteCount: count)
            }
        }
        _ = cka_ipc_send(cmdEp, UnsafeRawPointer(buf))
    }

    func recvByte() -> UInt8? {
        if evtEp == 0 {
            return nil
        }
        let msgSize = 8 + KS_IPC_MSG_MAX
        let buf = UnsafeMutableRawPointer.allocate(byteCount: msgSize, alignment: 8)
        defer { buf.deallocate() }

        let st = cka_ipc_recv(evtEp, buf)
        if st != KS_STATUS_OK {
            return nil
        }

        let tag = buf.load(as: UInt32.self)
        let len = buf.load(fromByteOffset: 4, as: UInt32.self)
        if tag != UART_TAG_RX_EVENT || len == 0 {
            return nil
        }
        return buf.load(fromByteOffset: 8, as: UInt8.self)
    }
}
@_cdecl("core_main_swift")
public func core_main_swift() -> Int32 {
    // Phase 1: Initialize a UART client and emit a basic log message.
    if let boot = core_boot_if() {
        let uart = UARTClient(cmdEp: boot.pointee.uart_cmd_ep,
                              evtEp: boot.pointee.uart_evt_ep)
        
        // String not available right now.
//        uart.write(Array("Core UARTClient online\n".utf8))
        
        let msg: [UInt8] = [
            0x43, 0x6F, 0x72, 0x65, 0x20, 0x55, 0x41, 0x52, 0x54, 0x43, 0x6C, 0x69,
            0x65, 0x6E, 0x74, 0x20, 0x6F, 0x6E, 0x6C, 0x69, 0x6E, 0x65, 0x0A
        ]
        uart.write(msg)
    }
    return 0
}
