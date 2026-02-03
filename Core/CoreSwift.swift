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

private let KLOG_TAG_WRITE: UInt32 = 1

private var gKernelLogEp: UInt64 = 0
private var gUartCmdEp: UInt64 = 0
private var gUartEvtEp: UInt64 = 0
private var gUartReady: Bool = false
private var gConsole: ConsoleService? = nil
private var gBlockDevice: BlockDevice? = nil
private var gConsoleTask: TaskContext? = nil

private let CAP_R_CONSOLE_WRITE: UInt32 = 1 << 24
private let CAP_R_CONSOLE_READ: UInt32 = 1 << 25
private let CAP_R_FILE_READ: UInt32 = 1 << 26
private let CAP_R_DIR_LIST: UInt32 = 1 << 27
private let IO_INTENT_LATENCY: UInt32 = 1
private let IO_INTENT_THROUGHPUT: UInt32 = 2
private let IO_INTENT_BACKGROUND: UInt32 = 3
private let KS_STATUS_INVALID_ARG: Int32 = -1
private let KS_STATUS_NO_RIGHTS: Int32 = -3
private let KS_STATUS_BUSY: Int32 = -8

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
private let ansiGreen: [UInt8] = [0x1B, 0x5B, 0x33, 0x32, 0x6D]        // ESC[32m
private let ansiYellowBold: [UInt8] = [0x1B, 0x5B, 0x31, 0x3B, 0x33, 0x33, 0x6D] // ESC[1;33m
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
private func u64ToDecBytes(_ v: UInt64) -> [UInt8] {
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
    let coreWord = Array(staticStringBytes(" Core"))
    let buildLabel = Array(staticStringBytes("Build: "))
    let dateLabel = Array(staticStringBytes(" Date: "))
    let unknown = Array(staticStringBytes("unknown"))
    
    let ver = info.map { cStringBytes($0.core_version) } ?? unknown
    let buildNum = u64ToDecBytes(info?.kernel_build_number ?? 0)
    
    let name = info.map { cStringBytes($0.core_name) } ?? unknown
    let rawDate = info.map { cStringBytes($0.build_date) } ?? unknown
    // Prefer YYYY-MM-DD if it looks like a full timestamp.
    let date = rawDate.count >= 10 ? Array(rawDate[0..<10]) : rawDate
    let line1Width = name.count + coreWord.count + 1 + ver.count
    let line2Width = buildLabel.count + buildNum.count + dateLabel.count + date.count
    let maxWidth = max(line1Width, line2Width)
    let innerWidth = maxWidth + 2 // 1 space padding on both sides
    
    writeBoxBorder(uart, innerWidth: innerWidth)
    
    // Line 1: "Core x.x.x" (Core is yellow + bold, rest green)
    writeBoxLine(uart, maxTextWidth: maxWidth, textWidth: line1Width) {
        uart.write(ansiYellowBold)
        uart.write(name)
        uart.write(coreWord)
        uart.write(ansiResetAll)
        uart.write(ansiGreen)
        uart.write([0x20])
        uart.write(ver)
    }
    
    // Line 2: "Build: N Date: YYYY-MM-DD" (all green)
    writeBoxLine(uart, maxTextWidth: maxWidth, textWidth: line2Width) {
        uart.write(ansiGreen)
        uart.write(buildLabel)
        uart.write(buildNum)
        uart.write(dateLabel)
        uart.write(date)
    }
    
    writeBoxBorder(uart, innerWidth: innerWidth)
    uart.write([0x0D, 0x0A])
}

struct ConsoleService {
    var uart: UARTClient
    var inputBuffer: [UInt8] = []
    var lineQueue: [[UInt8]] = []
    var history: [[UInt8]] = []
    var historyIndex: Int? = nil
    var escapeState: UInt8 = 0
    let prompt: [UInt8] = [0x3E, 0x20] // "> "
    let ansiYellow: [UInt8] = [0x1B, 0x5B, 0x33, 0x33, 0x6D]
    let ansiPrompt: [UInt8] = [0x1B, 0x5B, 0x33, 0x32, 0x6D] // green
    let ansiReset: [UInt8] = [0x1B, 0x5B, 0x30, 0x6D]
    let ansiClearLine: [UInt8] = [0x1B, 0x5B, 0x32, 0x4B] // ESC[2K
    let ansiClearScreen: [UInt8] = [0x1B, 0x5B, 0x32, 0x4A] // ESC[2J
    let ansiHome: [UInt8] = [0x1B, 0x5B, 0x48] // ESC[H
    
    mutating func writeUserOutput(_ s: StaticString) {
        let b = staticStringBytes(s)
        uart.write(Array(b))
    }
    
    mutating func writeKernelLog(_ data: [UInt8]) {
        if !inputBuffer.isEmpty {
            uart.write([0x0D, 0x0A])
        }
        uart.write(ansiYellow)
        uart.write(data)
        uart.write(ansiReset)
        uart.write([0x0D, 0x0A])
        redrawPrompt()
    }
    
    mutating func writeUserOutput(_ data: [UInt8]) {
        uart.write(data)
    }
    
    mutating func handleRxByte(_ b: UInt8) {
        if escapeState != 0 {
            handleEscapeByte(b)
            return
        }
        if b == 0x1B {
            escapeState = 1
            return
        }
        switch b {
        case 0x08, 0x7F: // backspace/delete
            if !inputBuffer.isEmpty {
                inputBuffer.removeLast()
                uart.write([0x08, 0x20, 0x08])
            }
        case 0x0D, 0x0A: // CR/LF
            uart.write([0x0D, 0x0A])
            if !inputBuffer.isEmpty {
                enqueueLine(inputBuffer)
                processLine(inputBuffer)
                rememberHistory(inputBuffer)
                inputBuffer.removeAll(keepingCapacity: true)
            }
            historyIndex = nil
            redrawPrompt()
        default:
            if inputBuffer.count < KS_IPC_MSG_MAX {
                inputBuffer.append(b)
                uart.write([b])
            }
        }
    }
    
    mutating func enqueueLine(_ line: [UInt8]) {
        if lineQueue.count >= 8 {
            lineQueue.removeFirst()
        }
        lineQueue.append(line)
    }
    
    mutating func rememberHistory(_ line: [UInt8]) {
        if line.isEmpty {
            return
        }
        if history.count >= 16 {
            history.removeFirst()
        }
        history.append(line)
    }
    
    mutating func popLine() -> [UInt8]? {
        if lineQueue.isEmpty {
            return nil
        }
        return lineQueue.removeFirst()
    }
    
    mutating func redrawPrompt() {
        uart.write(ansiPrompt)
        uart.write(prompt)
        uart.write(ansiReset)   // so user input is normal color
        if !inputBuffer.isEmpty {
            uart.write(inputBuffer)
        }
    }
    
    mutating func redrawLine(_ line: [UInt8]) {
        uart.write([0x0D])
        uart.write(ansiClearLine)
        inputBuffer = line
        redrawPrompt()
    }
    
    mutating func handleEscapeByte(_ b: UInt8) {
        if escapeState == 1 {
            if b == 0x5B {
                escapeState = 2
            } else {
                escapeState = 0
            }
            return
        }
        if escapeState == 2 {
            switch b {
            case 0x41: // Up
                if !history.isEmpty {
                    if historyIndex == nil {
                        historyIndex = history.count - 1
                    } else if let idx = historyIndex, idx > 0 {
                        historyIndex = idx - 1
                    }
                    if let idx = historyIndex {
                        redrawLine(history[idx])
                    }
                }
            case 0x42: // Down
                if let idx = historyIndex {
                    if idx + 1 < history.count {
                        historyIndex = idx + 1
                        redrawLine(history[idx + 1])
                    } else {
                        historyIndex = nil
                        redrawLine([])
                    }
                }
            default:
                break
            }
            escapeState = 0
            return
        }
        escapeState = 0
    }
    
    mutating func processLine(_ line: [UInt8]) {
        let tokens = splitTokens(line)
        if tokens.isEmpty { return }
        if bytesEqual(tokens[0], "help") {
            writeUserOutput("\r\n")
            writeUserOutput("help - show help\r\n")
            writeUserOutput("clear - clear screen\r\n")
            writeUserOutput("ls - list root directory\r\n")
            writeUserOutput("open <path> - issue file read capability\r\n")
            writeUserOutput("cat <path> - read file using cap\r\n")
            writeUserOutput("testtask <path> - read-only task (no list)\r\n")
            writeUserOutput("ioreadtest <a> <b> - latency vs background read\r\n")
            return
        }
        
        if bytesEqual(tokens[0], "clear") {
            uart.write(ansiClearScreen)
            uart.write(ansiHome)
            return
        }
        
        if bytesEqual(tokens[0], "ls") {
            writeUserOutput("\r\n")
            if var task = gConsoleTask {
                listFat16RootDirectory(task: &task)
                gConsoleTask = task
            }
            return
        }
        
        if bytesEqual(tokens[0], "devices") {
            writeUserOutput("\r\n")
            writeUserOutput("[dev] listing devices...\r\n")
            _ = cka_dtb_dump_devices()
            return
        }
        
        if bytesEqual(tokens[0], "open") {
            writeUserOutput("\r\n")
            if tokens.count < 2 {
                writeUserOutput("[fs] open: missing path\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] open: invalid path\r\n")
                return
            }
            let cap = issueFileCap(path: norm, rights: CAP_R_FILE_READ)
            if cap == 0 {
                writeUserOutput("[fs] open: cap table full\r\n")
                return
            }
            if var task = gConsoleTask {
                task.addCap(cap)
                gConsoleTask = task
            }
            writeUserOutput("[fs] opened cap ")
            writeUserOutput(u64ToDecBytes(UInt64(cap)))
            writeUserOutput("\r\n")
            return
        }
        
        if bytesEqual(tokens[0], "cat") {
            writeUserOutput("\r\n")
            if tokens.count < 2 {
                writeUserOutput("[fs] cat: missing path\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] cat: invalid path\r\n")
                return
            }
            if var task = gConsoleTask {
                readFat16File(task: &task, path: norm)
                gConsoleTask = task
            }
            return
        }
        
        if bytesEqual(tokens[0], "testtask") {
            writeUserOutput("\r\n")
            if tokens.count < 2 {
                writeUserOutput("[fs] testtask: missing path\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] testtask: invalid path\r\n")
                return
            }
            runTestTask(path: norm)
            return
        }
        
        if bytesEqual(tokens[0], "ioreadtest") {
            writeUserOutput("\r\n")
            if tokens.count < 3 {
                writeUserOutput("[fs] ioreadtest: missing paths\r\n")
                return
            }
            guard let pathA = normalizePath(tokens[1]),
                  let pathB = normalizePath(tokens[2]) else {
                writeUserOutput("[fs] ioreadtest: invalid path\r\n")
                return
            }
            runIoReadTest(pathA: pathA, pathB: pathB)
            return
        }
    }
}

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

private func readU16LE(_ data: [UInt8], _ offset: Int) -> UInt16 {
    if data.count < offset + 2 {
        return 0
    }
    return UInt16(data[offset + 0]) | (UInt16(data[offset + 1]) << 8)
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

private func splitTokens(_ line: [UInt8]) -> [[UInt8]] {
    var tokens: [[UInt8]] = []
    var i = 0
    while i < line.count {
        while i < line.count && line[i] == 0x20 { i += 1 }
        if i >= line.count { break }
        let start = i
        while i < line.count && line[i] != 0x20 { i += 1 }
        if start < i {
            tokens.append(Array(line[start..<i]))
        }
    }
    return tokens
}

//private func u64ToDecBytes(_ value: UInt64) -> [UInt8] {
//    if value == 0 { return [0x30] }
//    var v = value
//    var tmp: [UInt8] = []
//    while v > 0 {
//        let digit = UInt8(v % 10)
//        tmp.append(0x30 + digit)
//        v /= 10
//    }
//    return tmp.reversed()
//}

private func toUpperASCII(_ b: UInt8) -> UInt8 {
    if b >= 0x61 && b <= 0x7A {
        return b - 0x20
    }
    return b
}

private func normalizePath(_ raw: [UInt8]) -> [UInt8]? {
    if raw.isEmpty { return nil }
    var start = 0
    var end = raw.count
    while start < end && raw[start] == 0x20 { start += 1 }
    while end > start && raw[end - 1] == 0x20 { end -= 1 }
    if start >= end { return nil }
    
    var path = Array(raw[start..<end])
    if path.count == 1 && path[0] == 0x2F { return [0x2F] }
    if path.first == 0x2F { path.removeFirst() }
    if path.isEmpty { return nil }
    for b in path {
        if b == 0x2F || b == 0x5C || b == 0x3A { return nil }
    }
    if path.count >= 2 {
        for i in 0..<(path.count - 1) {
            if path[i] == 0x2E && path[i + 1] == 0x2E { return nil }
        }
    }
    var name: [UInt8] = []
    var ext: [UInt8] = []
    var seenDot = false
    for b in path {
        if b == 0x2E {
            if seenDot { return nil }
            seenDot = true
            continue
        }
        let up = toUpperASCII(b)
        if !seenDot {
            name.append(up)
        } else {
            ext.append(up)
        }
    }
    if name.isEmpty { return nil }
    if name.count > 8 || ext.count > 3 { return nil }
    var out: [UInt8] = []
    out.append(contentsOf: name)
    if !ext.isEmpty {
        out.append(0x2E)
        out.append(contentsOf: ext)
    }
    return out
}

private struct BlockDevice {
    let sectorSize: UInt32
    let capacitySectors: UInt64
    
    func read(lba: UInt64, count: UInt32, intent: UInt32) -> [UInt8]? {
        if count == 0 {
            return []
        }
        let byteCount = Int(count) * Int(sectorSize)
        var buf = [UInt8](repeating: 0, count: byteCount)
        let st = buf.withUnsafeMutableBytes { raw -> Int32 in
            guard let base = raw.baseAddress else { return KS_STATUS_INVALID_ARG }
            return cka_block_read_intent(lba, count, base, byteCount, intent)
        }
        if st != KS_STATUS_OK {
            return nil
        }
        return buf
    }
}

private struct Fat16Volume {
    let bytesPerSector: UInt16
    let sectorsPerCluster: UInt8
    let reservedSectors: UInt16
    let numFATs: UInt8
    let rootEntryCount: UInt16
    let sectorsPerFAT: UInt16
    let totalSectors: UInt32
    let rootDirStartLBA: UInt32
    let rootDirSectors: UInt32
    let fatStartLBA: UInt32
    let dataStartLBA: UInt32
}

private func parseFat16BootSector(_ data: [UInt8]) -> Fat16Volume? {
    if data.count < 64 { return nil }
    let bytesPerSector = readU16LE(data, 11)
    let sectorsPerCluster = data[13]
    let reservedSectors = readU16LE(data, 14)
    let numFATs = data[16]
    let rootEntryCount = readU16LE(data, 17)
    let totalSectors16 = readU16LE(data, 19)
    let sectorsPerFAT = readU16LE(data, 22)
    let totalSectors32 = readU32LE(data, 32)
    
    let totalSectors = totalSectors16 != 0 ? UInt32(totalSectors16) : totalSectors32
    if bytesPerSector == 0 || sectorsPerFAT == 0 || totalSectors == 0 {
        return nil
    }
    
    let rootDirSectors = UInt32((UInt32(rootEntryCount) * 32 + UInt32(bytesPerSector) - 1) / UInt32(bytesPerSector))
    let rootDirStartLBA = UInt32(reservedSectors) + UInt32(numFATs) * UInt32(sectorsPerFAT)
    let fatStartLBA = UInt32(reservedSectors)
    let dataStartLBA = rootDirStartLBA + rootDirSectors
    
    return Fat16Volume(bytesPerSector: bytesPerSector,
                       sectorsPerCluster: sectorsPerCluster,
                       reservedSectors: reservedSectors,
                       numFATs: numFATs,
                       rootEntryCount: rootEntryCount,
                       sectorsPerFAT: sectorsPerFAT,
                       totalSectors: totalSectors,
                       rootDirStartLBA: rootDirStartLBA,
                       rootDirSectors: rootDirSectors,
                       fatStartLBA: fatStartLBA,
                       dataStartLBA: dataStartLBA)
}

private func listFat16RootDirectory(task: inout TaskContext) {
    if !task.canListRoot() {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return
    }
    guard let boot = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read boot sector\r\n")))
        return
    }
    guard let vol = parseFat16BootSector(boot) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT16 volume\r\n")))
        return
    }
    if vol.rootDirSectors == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty root directory\r\n")))
        return
    }
    var rootData: [UInt8] = []
    for i in 0..<vol.rootDirSectors {
        if let part = block.read(lba: UInt64(vol.rootDirStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            rootData.append(contentsOf: part)
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read root directory\r\n")))
            return
        }
    }
    let entryCount = rootData.count / 32
    gConsole?.writeUserOutput(Array(staticStringBytes("FAT16 root:\r\n")))
    func trimSpaces(_ bytes: [UInt8]) -> [UInt8] {
        var start = 0
        var end = bytes.count
        while start < end && bytes[start] == 0x20 { start += 1 }
        while end > start && bytes[end - 1] == 0x20 { end -= 1 }
        if start >= end { return [] }
        return Array(bytes[start..<end])
    }
    for i in 0..<entryCount {
        let off = i * 32
        let first = rootData[off]
        if first == 0x00 { break }
        if first == 0xE5 { continue }
        let attr = rootData[off + 11]
        if attr == 0x0F { continue }
        if (attr & 0x08) != 0 { continue } // volume label
        let nameBytes = trimSpaces(Array(rootData[off..<(off + 8)]))
        let extBytes = trimSpaces(Array(rootData[(off + 8)..<(off + 11)]))
        if nameBytes.isEmpty { continue }
        var line: [UInt8] = [0x20, 0x20]
        line.append(contentsOf: nameBytes)
        if !extBytes.isEmpty {
            line.append(0x2E)
            line.append(contentsOf: extBytes)
        }
        line.append(0x0D)
        line.append(0x0A)
        gConsole?.writeUserOutput(line)
    }
}

private struct FileCapEntry {
    let id: UInt32
    let rights: UInt32
    let path: [UInt8]
}

private var gFileCaps: [FileCapEntry] = []
private var gNextFileCapId: UInt32 = 1

private func issueFileCap(path: [UInt8], rights: UInt32) -> UInt32 {
    if gFileCaps.count >= 64 { return 0 }
    let id = gNextFileCapId
    gNextFileCapId &+= 1
    gFileCaps.append(FileCapEntry(id: id, rights: rights, path: path))
    return id
}

private func lookupFileCap(_ id: UInt32) -> FileCapEntry? {
    for entry in gFileCaps {
        if entry.id == id { return entry }
    }
    return nil
}

private struct TaskContext {
    var capIds: [UInt32] = []
    
    mutating func addCap(_ id: UInt32) {
        capIds.append(id)
    }
    
    func hasRights(path: [UInt8], rights: UInt32) -> Bool {
        for id in capIds {
            if let entry = lookupFileCap(id) {
                if entry.path == path && (entry.rights & rights) == rights {
                    return true
                }
            }
        }
        return false
    }
    
    func canListRoot() -> Bool {
        return hasRights(path: [0x2F], rights: CAP_R_DIR_LIST)
    }
    
    func canReadFile(path: [UInt8]) -> Bool {
        return hasRights(path: path, rights: CAP_R_FILE_READ)
    }
}

private func readFat16File(task: inout TaskContext, path: [UInt8]) {
    if !task.canReadFile(path: path) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return
    }
    guard let boot = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read boot sector\r\n")))
        return
    }
    guard let vol = parseFat16BootSector(boot) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT16 volume\r\n")))
        return
    }
    
    var rootData: [UInt8] = []
    for i in 0..<vol.rootDirSectors {
        if let part = block.read(lba: UInt64(vol.rootDirStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            rootData.append(contentsOf: part)
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read root directory\r\n")))
            return
        }
    }
    
    func trimSpaces(_ bytes: [UInt8]) -> [UInt8] {
        var start = 0
        var end = bytes.count
        while start < end && bytes[start] == 0x20 { start += 1 }
        while end > start && bytes[end - 1] == 0x20 { end -= 1 }
        if start >= end { return [] }
        return Array(bytes[start..<end])
    }
    
    var firstCluster: UInt16 = 0
    var fileSize: UInt32 = 0
    var found = false
    let entryCount = rootData.count / 32
    for i in 0..<entryCount {
        let off = i * 32
        let first = rootData[off]
        if first == 0x00 { break }
        if first == 0xE5 { continue }
        let attr = rootData[off + 11]
        if attr == 0x0F { continue }
        if (attr & 0x08) != 0 { continue }
        if (attr & 0x10) != 0 { continue }
        let nameBytes = trimSpaces(Array(rootData[off..<(off + 8)]))
        let extBytes = trimSpaces(Array(rootData[(off + 8)..<(off + 11)]))
        if nameBytes.isEmpty { continue }
        var norm: [UInt8] = []
        norm.append(contentsOf: nameBytes.map { toUpperASCII($0) })
        if !extBytes.isEmpty {
            norm.append(0x2E)
            norm.append(contentsOf: extBytes.map { toUpperASCII($0) })
        }
        if norm == path {
            firstCluster = readU16LE(rootData, off + 26)
            fileSize = readU32LE(rootData, off + 28)
            found = true
            break
        }
    }
    if !found {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        return
    }
    if fileSize == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty file\r\n")))
        return
    }
    
    var fatData: [UInt8] = []
    for i in 0..<UInt32(vol.sectorsPerFAT) {
        if let part = block.read(lba: UInt64(vol.fatStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            fatData.append(contentsOf: part)
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
            return
        }
    }
    
    let clusterSizeBytes = UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster)
    var remaining = fileSize
    var cluster = firstCluster
    var output: [UInt8] = []
    while cluster >= 2 && remaining > 0 {
        let dataLBA = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
        for s in 0..<UInt32(vol.sectorsPerCluster) {
            if let part = block.read(lba: dataLBA + UInt64(s), count: 1, intent: IO_INTENT_LATENCY) {
                output.append(contentsOf: part)
            } else {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read file data\r\n")))
                return
            }
        }
        let fatOff = Int(cluster) * 2
        if fatOff + 1 >= fatData.count { break }
        let next = readU16LE(fatData, fatOff)
        if next >= 0xFFF8 { break }
        if next == 0xFFF7 { break }
        cluster = next
    }
    
    if output.count > Int(fileSize) {
        output = Array(output[0..<Int(fileSize)])
    }
    gConsole?.writeUserOutput(output)
    gConsole?.writeUserOutput([0x0D, 0x0A])
}

private func runTestTask(path: [UInt8]) {
    var test = TaskContext()
    let fileCap = issueFileCap(path: path, rights: CAP_R_FILE_READ)
    if fileCap == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] testtask: cap table full\r\n")))
        return
    }
    test.addCap(fileCap)
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] testtask: list root -> ")))
    if test.canListRoot() {
        gConsole?.writeUserOutput(Array(staticStringBytes("allowed\r\n")))
    } else {
        gConsole?.writeUserOutput(Array(staticStringBytes("denied\r\n")))
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] testtask: read file\r\n")))
    readFat16File(task: &test, path: path)
}

private func runIoReadTest(pathA: [UInt8], pathB: [UInt8]) {
    var taskA = TaskContext()
    var taskB = TaskContext()
    let capA = issueFileCap(path: pathA, rights: CAP_R_FILE_READ)
    let capB = issueFileCap(path: pathB, rights: CAP_R_FILE_READ)
    if capA == 0 || capB == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] ioreadtest: cap table full\r\n")))
        return
    }
    taskA.addCap(capA)
    taskB.addCap(capB)
    gConsole?.writeUserOutput(Array(staticStringBytes("[iotest] A=LATENCY B=BACKGROUND\r\n")))
    
    guard var stateA = prepareFat16ReadState(task: taskA, path: pathA, intent: IO_INTENT_LATENCY, label: 0x41),
          var stateB = prepareFat16ReadState(task: taskB, path: pathB, intent: IO_INTENT_BACKGROUND, label: 0x42) else {
        return
    }
    
    gConsole?.writeUserOutput(Array(staticStringBytes("[iotest] interleaving clusters\r\n")))
    while stateA.remaining > 0 || stateB.remaining > 0 {
        if stateA.remaining > 0 {
            if !readOneCluster(&stateA) { break }
        }
        if stateB.remaining > 0 {
            if !readOneCluster(&stateB) { break }
        }
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[iotest] done\r\n")))
}

private struct Fat16ReadState {
    let vol: Fat16Volume
    let fat: [UInt8]
    var cluster: UInt16
    var remaining: UInt32
    let intent: UInt32
    let label: UInt8
}

private func prepareFat16ReadState(task: TaskContext, path: [UInt8], intent: UInt32, label: UInt8) -> Fat16ReadState? {
    if !task.canReadFile(path: path) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return nil
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return nil
    }
    guard let boot = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read boot sector\r\n")))
        return nil
    }
    guard let vol = parseFat16BootSector(boot) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT16 volume\r\n")))
        return nil
    }
    var rootData: [UInt8] = []
    for i in 0..<vol.rootDirSectors {
        if let part = block.read(lba: UInt64(vol.rootDirStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            rootData.append(contentsOf: part)
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read root directory\r\n")))
            return nil
        }
    }
    func trimSpaces(_ bytes: [UInt8]) -> [UInt8] {
        var start = 0
        var end = bytes.count
        while start < end && bytes[start] == 0x20 { start += 1 }
        while end > start && bytes[end - 1] == 0x20 { end -= 1 }
        if start >= end { return [] }
        return Array(bytes[start..<end])
    }
    var firstCluster: UInt16 = 0
    var fileSize: UInt32 = 0
    var found = false
    let entryCount = rootData.count / 32
    for i in 0..<entryCount {
        let off = i * 32
        let first = rootData[off]
        if first == 0x00 { break }
        if first == 0xE5 { continue }
        let attr = rootData[off + 11]
        if attr == 0x0F { continue }
        if (attr & 0x08) != 0 { continue }
        if (attr & 0x10) != 0 { continue }
        let nameBytes = trimSpaces(Array(rootData[off..<(off + 8)]))
        let extBytes = trimSpaces(Array(rootData[(off + 8)..<(off + 11)]))
        if nameBytes.isEmpty { continue }
        var norm: [UInt8] = []
        norm.append(contentsOf: nameBytes.map { toUpperASCII($0) })
        if !extBytes.isEmpty {
            norm.append(0x2E)
            norm.append(contentsOf: extBytes.map { toUpperASCII($0) })
        }
        if norm == path {
            firstCluster = readU16LE(rootData, off + 26)
            fileSize = readU32LE(rootData, off + 28)
            found = true
            break
        }
    }
    if !found {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        return nil
    }
    if fileSize == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty file\r\n")))
        return nil
    }
    var fatData: [UInt8] = []
    for i in 0..<UInt32(vol.sectorsPerFAT) {
        if let part = block.read(lba: UInt64(vol.fatStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            fatData.append(contentsOf: part)
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
            return nil
        }
    }
    return Fat16ReadState(vol: vol, fat: fatData, cluster: firstCluster, remaining: fileSize, intent: intent, label: label)
}

private func readOneCluster(_ state: inout Fat16ReadState) -> Bool {
    guard let block = gBlockDevice else { return false }
    if state.cluster < 2 || state.remaining == 0 {
        return true
    }
    let dataLBA = UInt64(state.vol.dataStartLBA) + UInt64(state.cluster - 2) * UInt64(state.vol.sectorsPerCluster)
    for s in 0..<UInt32(state.vol.sectorsPerCluster) {
        if let part = block.read(lba: dataLBA + UInt64(s), count: 1, intent: state.intent) {
            let readBytes = UInt32(part.count)
            if state.remaining > readBytes {
                state.remaining -= readBytes
            } else {
                state.remaining = 0
            }
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read file data\r\n")))
            return false
        }
    }
    let fatOff = Int(state.cluster) * 2
    if fatOff + 1 < state.fat.count {
        let next = readU16LE(state.fat, fatOff)
        if next >= 0xFFF8 || next == 0xFFF7 {
            state.remaining = 0
        } else {
            state.cluster = next
        }
    } else {
        state.remaining = 0
    }
    gConsole?.writeUserOutput([state.label, 0x2E])
    return true
}

private func readFat16FileWithIntent(task: inout TaskContext, path: [UInt8], intent: UInt32, label: StaticString) {
    if !task.canReadFile(path: path) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return
    }
    guard let boot = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read boot sector\r\n")))
        return
    }
    guard let vol = parseFat16BootSector(boot) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT16 volume\r\n")))
        return
    }
    
    var rootData: [UInt8] = []
    for i in 0..<vol.rootDirSectors {
        if let part = block.read(lba: UInt64(vol.rootDirStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            rootData.append(contentsOf: part)
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read root directory\r\n")))
            return
        }
    }
    
    func trimSpaces(_ bytes: [UInt8]) -> [UInt8] {
        var start = 0
        var end = bytes.count
        while start < end && bytes[start] == 0x20 { start += 1 }
        while end > start && bytes[end - 1] == 0x20 { end -= 1 }
        if start >= end { return [] }
        return Array(bytes[start..<end])
    }
    
    var firstCluster: UInt16 = 0
    var fileSize: UInt32 = 0
    var found = false
    let entryCount = rootData.count / 32
    for i in 0..<entryCount {
        let off = i * 32
        let first = rootData[off]
        if first == 0x00 { break }
        if first == 0xE5 { continue }
        let attr = rootData[off + 11]
        if attr == 0x0F { continue }
        if (attr & 0x08) != 0 { continue }
        if (attr & 0x10) != 0 { continue }
        let nameBytes = trimSpaces(Array(rootData[off..<(off + 8)]))
        let extBytes = trimSpaces(Array(rootData[(off + 8)..<(off + 11)]))
        if nameBytes.isEmpty { continue }
        var norm: [UInt8] = []
        norm.append(contentsOf: nameBytes.map { toUpperASCII($0) })
        if !extBytes.isEmpty {
            norm.append(0x2E)
            norm.append(contentsOf: extBytes.map { toUpperASCII($0) })
        }
        if norm == path {
            firstCluster = readU16LE(rootData, off + 26)
            fileSize = readU32LE(rootData, off + 28)
            found = true
            break
        }
    }
    if !found {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        return
    }
    if fileSize == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty file\r\n")))
        return
    }
    
    var fatData: [UInt8] = []
    for i in 0..<UInt32(vol.sectorsPerFAT) {
        if let part = block.read(lba: UInt64(vol.fatStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            fatData.append(contentsOf: part)
        } else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
            return
        }
    }
    
    gConsole?.writeUserOutput(Array(staticStringBytes(label)))
    gConsole?.writeUserOutput(Array(staticStringBytes(": start\r\n")))
    
    var remaining = fileSize
    var cluster = firstCluster
    let clusterSizeBytes = UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster)
    var totalRead: UInt32 = 0
    while cluster >= 2 && remaining > 0 {
        let dataLBA = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
        for s in 0..<UInt32(vol.sectorsPerCluster) {
            if let part = block.read(lba: dataLBA + UInt64(s), count: 1, intent: intent) {
                totalRead += UInt32(part.count)
            } else {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read file data\r\n")))
                return
            }
            if totalRead / clusterSizeBytes != (totalRead - UInt32(vol.bytesPerSector)) / clusterSizeBytes {
                gConsole?.writeUserOutput([0x2E])
            }
        }
        let fatOff = Int(cluster) * 2
        if fatOff + 1 >= fatData.count { break }
        let next = readU16LE(fatData, fatOff)
        if next >= 0xFFF8 { break }
        if next == 0xFFF7 { break }
        cluster = next
        if remaining > clusterSizeBytes {
            remaining -= clusterSizeBytes
        } else {
            remaining = 0
        }
    }
    
    gConsole?.writeUserOutput([0x0D, 0x0A])
    gConsole?.writeUserOutput(Array(staticStringBytes(label)))
    gConsole?.writeUserOutput(Array(staticStringBytes(": done\r\n")))
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
