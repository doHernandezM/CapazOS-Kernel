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
    var kernel_log_ep: UInt64
}

@_silgen_name("core_boot_if")
func core_boot_if() -> UnsafePointer<core_boot_if_t>?

@_silgen_name("cka_ipc_send")
func cka_ipc_send(_ endpoint: UInt64, _ msg: UnsafeRawPointer) -> Int32

@_silgen_name("cka_ipc_recv")
func cka_ipc_recv(_ endpoint: UInt64, _ msg: UnsafeMutableRawPointer) -> Int32

@_silgen_name("cka_ipc_try_recv")
func cka_ipc_try_recv(_ endpoint: UInt64, _ msg: UnsafeMutableRawPointer) -> Int32

@_silgen_name("core_dbg_uart_write")
func core_dbg_uart_write(_ buf: UnsafePointer<UInt8>?, _ len: Int) -> Void

@_silgen_name("cka_dtb_dump_devices")
func cka_dtb_dump_devices() -> Int32

struct KsBuildInfo {
    var kernel_build_number: UInt64
    var build_date: UnsafePointer<CChar>?
    var build_version: UnsafePointer<CChar>?
    var build_environment: UnsafePointer<CChar>?
    var kernel_version: UnsafePointer<CChar>?
    var kernel_platform: UnsafePointer<CChar>?
    var kernel_machine: UnsafePointer<CChar>?
    var core_name: UnsafePointer<CChar>?
    var core_version: UnsafePointer<CChar>?
}

@_silgen_name("cka_get_build_info")
func cka_get_build_info(_ outInfo: UnsafeMutablePointer<KsBuildInfo>) -> Int32

struct KsBlockInfo {
    var sector_size: UInt32
    var _pad0: UInt32
    var capacity_sectors: UInt64
}

@_silgen_name("cka_block_get_info")
func cka_block_get_info(_ outInfo: UnsafeMutablePointer<KsBlockInfo>) -> Int32

@_silgen_name("cka_block_read")
func cka_block_read(_ lba: UInt64, _ count: UInt32, _ buf: UnsafeMutableRawPointer, _ bufLen: Int) -> Int32

@_silgen_name("cka_block_read_intent")
func cka_block_read_intent(_ lba: UInt64, _ count: UInt32, _ buf: UnsafeMutableRawPointer, _ bufLen: Int, _ intent: UInt32) -> Int32

@_silgen_name("cka_block_write")
func cka_block_write(_ lba: UInt64, _ count: UInt32, _ buf: UnsafeRawPointer, _ bufLen: Int) -> Int32

@_silgen_name("cka_block_write_intent")
func cka_block_write_intent(_ lba: UInt64, _ count: UInt32, _ buf: UnsafeRawPointer, _ bufLen: Int, _ intent: UInt32) -> Int32

let KS_IPC_MSG_MAX: Int = 128
let KS_STATUS_OK: Int32 = 0

let UART_TAG_WRITE: UInt32 = 1
let UART_TAG_RX_EVENT: UInt32 = 2
let UART_TAG_QUERY_INTENTS: UInt32 = 3
let UART_TAG_OPEN_CONTRACT: UInt32 = 4

let UART_CONTRACT_CMD: UInt32 = 1
let UART_CONTRACT_EVT: UInt32 = 2

let KS_INTENT_DIR_CALL: UInt32 = 1
let KS_INTENT_DIR_EVENT: UInt32 = 2

let CAP_R_SEND: UInt32 = 1 << 12
let CAP_R_RECV: UInt32 = 1 << 13

let KLOG_TAG_WRITE: UInt32 = 1

var gKernelLogEp: UInt64 = 0
var gUartCmdEp: UInt64 = 0
var gUartEvtEp: UInt64 = 0
var gUartReady: Bool = false
var gConsole: ConsoleService? = nil
var gBlockDevice: BlockDevice? = nil
var gConsoleTask: TaskContext? = nil

let CAP_R_CONSOLE_WRITE: UInt32 = 1 << 24
let CAP_R_CONSOLE_READ: UInt32 = 1 << 25
let CAP_R_FILE_READ: UInt32 = 1 << 26
let CAP_R_DIR_LIST: UInt32 = 1 << 27
let CAP_R_FILE_WRITE_SAFE: UInt32 = 1 << 28
let CAP_R_FILE_WRITE_FAST: UInt32 = 1 << 29
let IO_INTENT_LATENCY: UInt32 = 1
let IO_INTENT_THROUGHPUT: UInt32 = 2
let IO_INTENT_BACKGROUND: UInt32 = 3
let FAT32_CONTRACT_R_READ: UInt32 = 1 << 0
let FAT32_CONTRACT_R_LIST: UInt32 = 1 << 1
let FAT32_CONTRACT_R_WRITE: UInt32 = 1 << 2
let FAT32_POLICY_SAFE: UInt32 = 1
let FAT32_POLICY_FAST: UInt32 = 2
let FAT32_POLICY_BACKGROUND: UInt32 = 3
let KS_STATUS_INVALID_ARG: Int32 = -1
let KS_STATUS_NO_RIGHTS: Int32 = -3
let KS_STATUS_BUSY: Int32 = -8

private func fetchBuildInfo() -> KsBuildInfo? {
    var info = KsBuildInfo(kernel_build_number: 0,
                           build_date: nil,
                           build_version: nil,
                           build_environment: nil,
                           kernel_version: nil,
                           kernel_platform: nil,
                           kernel_machine: nil,
                           core_name: nil,
                           core_version: nil)
    let st = cka_get_build_info(&info)
    return st == KS_STATUS_OK ? info : nil
}

private let ansiRed: [UInt8] = [0x1B, 0x5B, 0x33, 0x31, 0x6D]          // ESC[31m
private let ansiRedBold: [UInt8] = [0x1B, 0x5B, 0x31, 0x3B, 0x33, 0x31, 0x6D]   // ESC[1;31m
private let ansiGreen: [UInt8] = [0x1B, 0x5B, 0x33, 0x32, 0x6D]        // ESC[32m
private let ansiGreenBold: [UInt8] = [0x1B, 0x5B, 0x31, 0x3B, 0x33, 0x32, 0x6D] // ESC[1;32m
private let ansiBlue: [UInt8] = [0x1B, 0x5B, 0x33, 0x34, 0x6D]         // ESC[34m
private let ansiBlueBold: [UInt8] = [0x1B, 0x5B, 0x31, 0x3B, 0x33, 0x34, 0x6D]  // ESC[1;34m
private let ansiClearScreen: [UInt8] = [0x1B, 0x5B, 0x32, 0x4A] // ESC[2J
private let ansiYellowBold: [UInt8] = [0x1B, 0x5B, 0x31, 0x3B, 0x33, 0x33, 0x6D] // ESC[1;33m
private let ansiYellow: [UInt8] = [0x1B, 0x5B, 0x33, 0x3B, 0x33, 0x33, 0x6D] // ESC[33m
private let ansiResetAll: [UInt8] = [0x1B, 0x5B, 0x30, 0x6D]           // ESC[0m

@inline(__always)
private func repeated(_ byte: UInt8, count: Int) -> [UInt8] {
    if count <= 0 { return [] }
    return [UInt8](repeating: byte, count: count)
}

@inline(__always)
private func cStringBytes(_ s: UnsafePointer<CChar>?) -> [UInt8] {
    guard var p = s else { return [] }
    var out: [UInt8] = []
    out.reserveCapacity(32)
    while true {
        let c = p.pointee
        if c == 0 { break }
        out.append(UInt8(bitPattern: c))
        p = p.advanced(by: 1)
    }
    return out
}

@inline(__always)
func u64ToDecBytes(_ v: UInt64) -> [UInt8] {
    if v == 0 { return [0x30] } // "0"
    var x = v
    var tmp: [UInt8] = []
    tmp.reserveCapacity(20)
    while x > 0 {
        let digit = UInt8(x % 10)
        tmp.append(0x30 + digit)
        x /= 10
    }
    tmp.reverse()
    return tmp
}

@inline(__always)
private func writeBoxBorder(_ uart: UARTClient, innerWidth: Int) {
    uart.write(ansiRed)
    uart.write([0x2B]) // '+'
    uart.write(repeated(0x2D, count: innerWidth)) // '-'
    uart.write([0x2B]) // '+'
    uart.write(ansiResetAll)
    uart.write([0x0D, 0x0A])
}

@inline(__always)
private func writeBoxLine(_ uart: UARTClient,
                          maxTextWidth: Int,
                          textWidth: Int,
                          _ body: () -> Void) {
    uart.write(ansiRed)
    uart.write([0x7C]) // '|'
    uart.write(ansiGreen)
    uart.write([0x20]) // leading space
    body()
    if textWidth < maxTextWidth {
        uart.write(repeated(0x20, count: maxTextWidth - textWidth))
    }
    uart.write([0x20]) // trailing space
    uart.write(ansiRed)
    uart.write([0x7C]) // '|'
    uart.write(ansiResetAll)
    uart.write([0x0D, 0x0A])
}

private func emitCoreBanner(uart: UARTClient, info: KsBuildInfo?) {
    let buildLabel = Array(staticStringBytes("Build: "))
    let dateLabel = Array(staticStringBytes(" Date: "))
    let unknown = Array(staticStringBytes("unknown"))
    
    let ver = info.map { cStringBytes($0.core_version) } ?? unknown
    let buildNum = u64ToDecBytes(info?.kernel_build_number ?? 0)
    
    let name = info.map { cStringBytes($0.core_name) } ?? unknown
    let rawDate = info.map { cStringBytes($0.build_date) } ?? unknown
    // Prefer YYYY-MM-DD if it looks like a full timestamp.
    let date = rawDate.count >= 10 ? Array(rawDate[0..<10]) : rawDate
    let line1Width = name.count + 1 + ver.count
    let line2Width = buildLabel.count + buildNum.count + dateLabel.count + date.count
    let maxWidth = max(line1Width, line2Width)
    let innerWidth = maxWidth + 2 // 1 space padding on both sides
    
//    uart.write(ansiClearScreen)
    
    writeBoxBorder(uart, innerWidth: innerWidth)
    // Line 1: "Core x.x.x" (Core is yellow + bold, rest green)
    writeBoxLine(uart, maxTextWidth: maxWidth, textWidth: line1Width) {
        uart.write(ansiYellowBold)
        uart.write(name)
        uart.write(ansiResetAll)
        uart.write(ansiGreen)
        uart.write([0x20])
        uart.write(ver)
    }
    
    // Line 2: "Build: N Date: YYYY-MM-DD" (all green)
    writeBoxLine(uart, maxTextWidth: maxWidth, textWidth: line2Width) {
        uart.write(ansiGreenBold)
        uart.write(buildLabel)
        uart.write(ansiResetAll)
        uart.write(ansiGreen)
        uart.write(buildNum)
        uart.write(ansiGreenBold)
        uart.write(dateLabel)
        uart.write(ansiResetAll)
        uart.write(ansiGreen)
        uart.write(date)
    }
    
    writeBoxBorder(uart, innerWidth: innerWidth)
    uart.write([0x0D, 0x0A])
}

struct IntentDesc {
    let intentId: UInt32
    let direction: UInt32
    let maxLen: UInt32
    let schemaId: UInt32
    let rights: UInt32
}

struct IntentQueue<TxIntent, RxEvent> {
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
    let queue: IntentQueue<UInt32, UInt32>
    
    init(cmdEp: UInt64, evtEp: UInt64) {
        self.cmdEp = cmdEp
        self.evtEp = evtEp
        self.queue = IntentQueue<UInt32, UInt32>(cmdEp: cmdEp, evtEp: evtEp)
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
        
        let descSize = 20
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
            let schemaId = readU32LE(resp.data, offset + 12)
            let rights = readU32LE(resp.data, offset + 16)
            intents.append(IntentDesc(intentId: intentId,
                                      direction: direction,
                                      maxLen: maxLen,
                                      schemaId: schemaId,
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
    // Initialize a UART client and emit a basic log message.
    if let boot = core_boot_if() {
        // Do not block in core_main_swift: the UART driver pump only runs after
        // core_main returns. Use boot-seeded endpoints here and negotiate
        // contracts later from core_poll.
        let cmd = boot.pointee.uart_cmd_ep
        let evt = boot.pointee.uart_evt_ep
        let uart = UARTClient(cmdEp: cmd, evtEp: evt)
        gKernelLogEp = boot.pointee.kernel_log_ep
        gUartCmdEp = cmd
        gUartEvtEp = evt
        gUartReady = (cmd != 0 && evt != 0)
        gConsole = ConsoleService(uart: uart)
        var consoleTask = TaskContext()
        let rootCap = issueFileCap(path: [0x2F], rights: CAP_R_DIR_LIST)
        if rootCap != 0 {
            consoleTask.addCap(rootCap)
        }
        let contract = issueFat32Contract(rights: FAT32_CONTRACT_R_READ | FAT32_CONTRACT_R_LIST,
                                          policy: FAT32_POLICY_FAST)
        if contract != 0 {
            consoleTask.addContract(contract)
        }
        gConsoleTask = consoleTask
        var blkInfo = KsBlockInfo(sector_size: 0, _pad0: 0, capacity_sectors: 0)
        if cka_block_get_info(&blkInfo) == KS_STATUS_OK && blkInfo.sector_size != 0 {
            gBlockDevice = BlockDevice(sectorSize: blkInfo.sector_size,
                                       capacitySectors: blkInfo.capacity_sectors)
        }
        
        // Step 1: get build info, then print banner.
        let info = fetchBuildInfo()
        emitCoreBanner(uart: uart, info: info)
        gConsole?.redrawPrompt()
    }
    return 0
}

@_cdecl("core_poll")
public func core_poll() {
    if gKernelLogEp == 0 || !gUartReady || gConsole == nil {
        return
    }
    var console = gConsole!
    let msgSize = 8 + KS_IPC_MSG_MAX
    let buf = UnsafeMutableRawPointer.allocate(byteCount: msgSize, alignment: 8)
    defer { buf.deallocate() }
    
    for _ in 0..<64 {
        let st = cka_ipc_try_recv(gKernelLogEp, buf)
        if st != KS_STATUS_OK {
            break
        }
        let tag = buf.load(as: UInt32.self)
        let len = Int(buf.load(fromByteOffset: 4, as: UInt32.self))
        if tag != KLOG_TAG_WRITE || len <= 0 {
            continue
        }
        let count = min(len, KS_IPC_MSG_MAX)
        var data = [UInt8](repeating: 0, count: count)
        data.withUnsafeMutableBytes { dst in
            if let base = dst.baseAddress {
                base.copyMemory(from: buf.advanced(by: 8), byteCount: count)
            }
        }
        console.writeKernelLog(data)
    }
    
    for _ in 0..<64 {
        let st = cka_ipc_try_recv(gUartEvtEp, buf)
        if st != KS_STATUS_OK {
            break
        }
        let tag = buf.load(as: UInt32.self)
        let len = Int(buf.load(fromByteOffset: 4, as: UInt32.self))
        if tag != UART_TAG_RX_EVENT || len <= 0 {
            continue
        }
        let count = min(len, KS_IPC_MSG_MAX)
        if count <= 0 {
            continue
        }
        for i in 0..<count {
            let b = buf.load(fromByteOffset: 8 + i, as: UInt8.self)
            console.handleRxByte(b)
        }
    }
    gConsole = console
}

@_cdecl("core_console_write")
public func core_console_write(_ rights: UInt32,
                               _ buf: UnsafePointer<UInt8>?,
                               _ len: Int) -> Int32 {
    if (rights & CAP_R_CONSOLE_WRITE) == 0 {
        return KS_STATUS_NO_RIGHTS
    }
    if buf == nil || len <= 0 {
        return KS_STATUS_INVALID_ARG
    }
    guard var console = gConsole else {
        return KS_STATUS_BUSY
    }
    var data = [UInt8](repeating: 0, count: len)
    data.withUnsafeMutableBytes { dst in
        if let base = dst.baseAddress {
            base.copyMemory(from: buf!, byteCount: len)
        }
    }
    console.writeUserOutput(data)
    gConsole = console
    return KS_STATUS_OK
}

@_cdecl("core_console_read_line")
public func core_console_read_line(_ rights: UInt32,
                                   _ out: UnsafeMutablePointer<UInt8>?,
                                   _ maxLen: Int) -> Int32 {
    if (rights & CAP_R_CONSOLE_READ) == 0 {
        return KS_STATUS_NO_RIGHTS
    }
    if out == nil || maxLen <= 0 {
        return KS_STATUS_INVALID_ARG
    }
    guard var console = gConsole else {
        return KS_STATUS_BUSY
    }
    guard let line = console.popLine() else {
        gConsole = console
        return KS_STATUS_BUSY
    }
    let count = min(line.count, maxLen)
    for i in 0..<count {
        out!.advanced(by: i).pointee = line[i]
    }
    gConsole = console
    return Int32(count)
}
