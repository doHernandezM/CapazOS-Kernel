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
    var build_number: UInt64
    var build_date: UnsafePointer<CChar>?
    var build_version: UnsafePointer<CChar>?
    var build_environment: UnsafePointer<CChar>?
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

private let CAP_R_CONSOLE_WRITE: UInt32 = 1 << 24
private let CAP_R_CONSOLE_READ: UInt32 = 1 << 25
private let KS_STATUS_INVALID_ARG: Int32 = -1
private let KS_STATUS_NO_RIGHTS: Int32 = -3
private let KS_STATUS_BUSY: Int32 = -8

private func fetchBuildInfo() -> KsBuildInfo? {
    var info = KsBuildInfo(build_number: 0,
                           build_date: nil,
                           build_version: nil,
                           build_environment: nil)
    let st = cka_get_build_info(&info)
    return st == KS_STATUS_OK ? info : nil
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
        uart.write(prompt)
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
        if bytesEqual(line, "help") {
            writeUserOutput("\r\n")
            writeUserOutput("help - show help\r\n")
            writeUserOutput("clear - clear screen\r\n")
            writeUserOutput("ls - list root directory\r\n")
            return
        }
        
        if bytesEqual(line, "clear") {
            uart.write(ansiClearScreen)
            uart.write(ansiHome)
            return
        }

        if bytesEqual(line, "ls") {
            writeUserOutput("\r\n")
            listFat16RootDirectory()
            return
        }
        
        if bytesEqual(line, "devices") {
            writeUserOutput("\r\n")
            writeUserOutput("[dev] listing devices...\r\n")
            _ = cka_dtb_dump_devices()
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

private struct BlockDevice {
    let sectorSize: UInt32
    let capacitySectors: UInt64

    func read(lba: UInt64, count: UInt32) -> [UInt8]? {
        if count == 0 {
            return []
        }
        let byteCount = Int(count) * Int(sectorSize)
        var buf = [UInt8](repeating: 0, count: byteCount)
        let st = buf.withUnsafeMutableBytes { raw -> Int32 in
            guard let base = raw.baseAddress else { return KS_STATUS_INVALID_ARG }
            return cka_block_read(lba, count, base, byteCount)
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

    return Fat16Volume(bytesPerSector: bytesPerSector,
                       sectorsPerCluster: sectorsPerCluster,
                       reservedSectors: reservedSectors,
                       numFATs: numFATs,
                       rootEntryCount: rootEntryCount,
                       sectorsPerFAT: sectorsPerFAT,
                       totalSectors: totalSectors,
                       rootDirStartLBA: rootDirStartLBA,
                       rootDirSectors: rootDirSectors)
}

private func listFat16RootDirectory() {
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return
    }
    guard let boot = block.read(lba: 0, count: 1) else {
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
        if let part = block.read(lba: UInt64(vol.rootDirStartLBA + i), count: 1) {
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
        var blkInfo = KsBlockInfo(sector_size: 0, _pad0: 0, capacity_sectors: 0)
        if cka_block_get_info(&blkInfo) == KS_STATUS_OK && blkInfo.sector_size != 0 {
            gBlockDevice = BlockDevice(sectorSize: blkInfo.sector_size,
                                       capacitySectors: blkInfo.capacity_sectors)
        }
        
        gConsole?.writeUserOutput(Array(staticStringBytes("Core is online.\n")))
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
