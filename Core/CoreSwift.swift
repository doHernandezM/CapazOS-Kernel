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
private let UART_TAG_QUERY_INTENTS: UInt32 = 3
private let UART_TAG_OPEN_CONTRACT: UInt32 = 4

private let UART_CONTRACT_CMD: UInt32 = 1
private let UART_CONTRACT_EVT: UInt32 = 2

private let KS_INTENT_DIR_CALL: UInt32 = 1
private let KS_INTENT_DIR_EVENT: UInt32 = 2

private let CAP_R_SEND: UInt32 = 1 << 12
private let CAP_R_RECV: UInt32 = 1 << 13

private func writeU32LE(_ value: UInt32, _ out: inout [UInt8], _ offset: Int) {
    out[offset + 0] = UInt8(truncatingIfNeeded: value)
    out[offset + 1] = UInt8(truncatingIfNeeded: value >> 8)
    out[offset + 2] = UInt8(truncatingIfNeeded: value >> 16)
    out[offset + 3] = UInt8(truncatingIfNeeded: value >> 24)
}

private func readU32LE(_ data: [UInt8], _ offset: Int) -> UInt32 {
    if data.count < offset + 4 {
        return 0
    }
    return UInt32(data[offset + 0])
        | (UInt32(data[offset + 1]) << 8)
        | (UInt32(data[offset + 2]) << 16)
        | (UInt32(data[offset + 3]) << 24)
}

private func readU64LE(_ data: [UInt8], _ offset: Int) -> UInt64 {
    if data.count < offset + 8 {
        return 0
    }
    var value: UInt64 = 0
    for i in 0..<8 {
        value |= UInt64(data[offset + i]) << (UInt64(i) * 8)
    }
    return value
}

struct IntentDesc {
    let intentId: UInt32
    let direction: UInt32
    let maxLen: UInt32
    let rights: UInt32
}

struct IntentQueue {
    let cmdEp: UInt64
    let evtEp: UInt64

    func send(tag: UInt32, bytes: [UInt8]) {
        if cmdEp == 0 {
            return
        }
        let count = min(bytes.count, KS_IPC_MSG_MAX)
        let msgSize = 8 + KS_IPC_MSG_MAX
        let buf = UnsafeMutableRawPointer.allocate(byteCount: msgSize, alignment: 8)
        defer { buf.deallocate() }

        buf.storeBytes(of: tag, as: UInt32.self)
        buf.storeBytes(of: UInt32(count), toByteOffset: 4, as: UInt32.self)
        bytes.withUnsafeBytes { src in
            if let base = src.baseAddress {
                buf.advanced(by: 8).copyMemory(from: base, byteCount: count)
            }
        }
        _ = cka_ipc_send(cmdEp, UnsafeRawPointer(buf))
    }

    func recv() -> (tag: UInt32, data: [UInt8])? {
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
        let len = Int(buf.load(fromByteOffset: 4, as: UInt32.self))
        if len <= 0 {
            return (tag, [])
        }
        let count = min(len, KS_IPC_MSG_MAX)
        var data = [UInt8](repeating: 0, count: count)
        data.withUnsafeMutableBytes { dst in
            if let base = dst.baseAddress {
                base.copyMemory(from: buf.advanced(by: 8), byteCount: count)
            }
        }
        return (tag, data)
    }
}

struct UARTClient {
    let cmdEp: UInt64
    let evtEp: UInt64
    let queue: IntentQueue

    init(cmdEp: UInt64, evtEp: UInt64) {
        self.cmdEp = cmdEp
        self.evtEp = evtEp
        self.queue = IntentQueue(cmdEp: cmdEp, evtEp: evtEp)
    }

    func write(_ bytes: [UInt8]) {
        queue.send(tag: UART_TAG_WRITE, bytes: bytes)
    }

    func queryIntents() -> [IntentDesc] {
        if cmdEp == 0 || evtEp == 0 {
            return []
        }
        queue.send(tag: UART_TAG_QUERY_INTENTS, bytes: [])
        guard let resp = queue.recv() else {
            return []
        }
        if resp.tag != UART_TAG_QUERY_INTENTS || resp.data.count < 4 {
            return []
        }

        let count = readU32LE(resp.data, 0)
        if count == 0 {
            return []
        }

        let descSize = 16
        var intents: [IntentDesc] = []
        intents.reserveCapacity(Int(count))
        for i in 0..<Int(count) {
            let offset = 4 + i * descSize
            if resp.data.count < offset + descSize {
                break
            }
            let intentId = readU32LE(resp.data, offset)
            let direction = readU32LE(resp.data, offset + 4)
            let maxLen = readU32LE(resp.data, offset + 8)
            let rights = readU32LE(resp.data, offset + 12)
            intents.append(IntentDesc(intentId: intentId,
                                      direction: direction,
                                      maxLen: maxLen,
                                      rights: rights))
        }
        return intents
    }

    func openContract(kind: UInt32, rights: UInt32) -> UInt64? {
        if cmdEp == 0 || evtEp == 0 {
            return nil
        }
        var payload = [UInt8](repeating: 0, count: 8)
        writeU32LE(kind, &payload, 0)
        writeU32LE(rights, &payload, 4)
        queue.send(tag: UART_TAG_OPEN_CONTRACT, bytes: payload)

        guard let resp = queue.recv() else {
            return nil
        }
        if resp.tag != UART_TAG_OPEN_CONTRACT || resp.data.count < 16 {
            return nil
        }
        let status = Int32(bitPattern: readU32LE(resp.data, 0))
        if status != KS_STATUS_OK {
            return nil
        }
        let cap = readU64LE(resp.data, 8)
        return cap == 0 ? nil : cap
    }

    func recvByte() -> UInt8? {
        if evtEp == 0 {
            return nil
        }
        guard let resp = queue.recv() else {
            return nil
        }
        if resp.tag != UART_TAG_RX_EVENT || resp.data.isEmpty {
            return nil
        }
        return resp.data[0]
    }
}
@_cdecl("core_main_swift")
public func core_main_swift() -> Int32 {
    // Phase 1: Initialize a UART client and emit a basic log message.
    if let boot = core_boot_if() {
        let base = UARTClient(cmdEp: boot.pointee.uart_cmd_ep,
                              evtEp: boot.pointee.uart_evt_ep)
        _ = base.queryIntents()
        let cmd = base.openContract(kind: UART_CONTRACT_CMD, rights: CAP_R_SEND) ?? boot.pointee.uart_cmd_ep
        let evt = base.openContract(kind: UART_CONTRACT_EVT, rights: CAP_R_RECV) ?? boot.pointee.uart_evt_ep
        let uart = UARTClient(cmdEp: cmd, evtEp: evt)
        let msg: [UInt8] = [
            0x43, 0x6F, 0x72, 0x65, 0x20, 0x55, 0x41, 0x52, 0x54, 0x43, 0x6C, 0x69,
            0x65, 0x6E, 0x74, 0x20, 0x6F, 0x6E, 0x6C, 0x69, 0x6E, 0x65, 0x0A
        ]
        uart.write(msg)
    }
    return 0
}
