//
//  Console.swift
//  Core
//

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
            writeUserOutput("ls [path] - list directory\r\n")
            writeUserOutput("opencontract fat32 <read|write|list> <safe|fast|background>\r\n")
            writeUserOutput("open <path> - issue file read capability\r\n")
            writeUserOutput("openw <path> <safe|fast> - issue file write capability\r\n")
            writeUserOutput("cat <path> - read file using cap\r\n")
            writeUserOutput("write <path> <data> - write file using cap\r\n")
            writeUserOutput("testtask <path> - read-only task (no list)\r\n")
            writeUserOutput("ioreadtest <a> <b> - latency vs background read\r\n")
            writeUserOutput("kerneltest <path> - read kernel file via caps/contracts\r\n")
            writeUserOutput("fsstats - show FAT32 stats\r\n")
            writeUserOutput("fsstatsreset - reset FAT32 stats\r\n")
            writeUserOutput("fstress <path> <iters> <size> <safe|fast> - FAT32 stress test\r\n")
            writeUserOutput("findkernel - locate kernel image\r\n")
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
                if tokens.count >= 2 {
                    guard let norm = normalizePath(tokens[1]) else {
                        writeUserOutput("[fs] ls: invalid path\r\n")
                        return
                    }
                    listFat32Directory(task: &task, path: norm)
                } else {
                    let root = NormalizedPath(flat: [0x2F], segments: [], isRoot: true)
                    listFat32Directory(task: &task, path: root)
                }
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

        if bytesEqual(tokens[0], "opencontract") {
            writeUserOutput("\r\n")
            if tokens.count < 4 {
                writeUserOutput("[fs] opencontract: usage opencontract fat32 <read|write|list> <safe|fast|background>\r\n")
                return
            }
            if !bytesEqual(tokens[1], "fat32") {
                writeUserOutput("[fs] opencontract: unknown kind\r\n")
                return
            }
            guard let rights = parseContractRights(tokens[2]) else {
                writeUserOutput("[fs] opencontract: invalid rights\r\n")
                return
            }
            var policy: UInt32 = 0
            if bytesEqual(tokens[3], "safe") {
                policy = FAT32_POLICY_SAFE
            } else if bytesEqual(tokens[3], "fast") {
                policy = FAT32_POLICY_FAST
            } else if bytesEqual(tokens[3], "background") {
                policy = FAT32_POLICY_BACKGROUND
            } else {
                writeUserOutput("[fs] opencontract: invalid policy\r\n")
                return
            }
            if (rights & FAT32_CONTRACT_R_WRITE) != 0 && policy == FAT32_POLICY_BACKGROUND {
                writeUserOutput("[fs] opencontract: write cannot be background\r\n")
                return
            }
            let cap = issueFat32Contract(rights: rights, policy: policy)
            if cap == 0 {
                writeUserOutput("[fs] opencontract: contract table full\r\n")
                return
            }
            if var task = gConsoleTask {
                task.addContract(cap)
                gConsoleTask = task
            }
            writeUserOutput("[fs] opened contract ")
            writeUserOutput(u64ToDecBytes(UInt64(cap)))
            writeUserOutput("\r\n")
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
            if norm.isRoot {
                writeUserOutput("[fs] open: cannot open root\r\n")
                return
            }
            let cap = issueFileCap(path: norm.flat, rights: CAP_R_FILE_READ)
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

        if bytesEqual(tokens[0], "openw") {
            writeUserOutput("\r\n")
            if tokens.count < 3 {
                writeUserOutput("[fs] openw: missing path or policy\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] openw: invalid path\r\n")
                return
            }
            if norm.isRoot {
                writeUserOutput("[fs] openw: cannot open root\r\n")
                return
            }
            var rights: UInt32 = 0
            if bytesEqual(tokens[2], "safe") {
                rights = CAP_R_FILE_WRITE_SAFE
            } else if bytesEqual(tokens[2], "fast") {
                rights = CAP_R_FILE_WRITE_FAST
            } else {
                writeUserOutput("[fs] openw: invalid policy\r\n")
                return
            }
            let cap = issueFileCap(path: norm.flat, rights: rights)
            if cap == 0 {
                writeUserOutput("[fs] openw: cap table full\r\n")
                return
            }
            if var task = gConsoleTask {
                task.addCap(cap)
                gConsoleTask = task
            }
            writeUserOutput("[fs] opened write cap ")
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
            if norm.isRoot {
                writeUserOutput("[fs] cat: invalid path\r\n")
                return
            }
            if var task = gConsoleTask {
                readFat32File(task: &task, path: norm)
                gConsoleTask = task
            }
            return
        }

        if bytesEqual(tokens[0], "write") {
            writeUserOutput("\r\n")
            if tokens.count < 3 {
                writeUserOutput("[fs] write: missing path or data\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] write: invalid path\r\n")
                return
            }
            if norm.isRoot {
                writeUserOutput("[fs] write: invalid path\r\n")
                return
            }
            if var task = gConsoleTask {
                let data = tokens[2]
                writeFat32File(task: &task, path: norm, data: data, safe: true)
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
            if norm.isRoot {
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
            if pathA.isRoot || pathB.isRoot {
                writeUserOutput("[fs] ioreadtest: invalid path\r\n")
                return
            }
            runIoReadTest(pathA: pathA, pathB: pathB)
            return
        }

        if bytesEqual(tokens[0], "kerneltest") {
            writeUserOutput("\r\n")
            if tokens.count < 2 {
                writeUserOutput("[fs] kerneltest: missing path\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] kerneltest: invalid path\r\n")
                return
            }
            if norm.isRoot {
                writeUserOutput("[fs] kerneltest: invalid path\r\n")
                return
            }
            runKernelReadTest(path: norm)
            return
        }

        if bytesEqual(tokens[0], "fsstats") {
            writeUserOutput("\r\n")
            fat32StatsPrint()
            return
        }

        if bytesEqual(tokens[0], "fsstatsreset") {
            writeUserOutput("\r\n")
            fat32StatsReset()
            writeUserOutput("[fs] stats reset\r\n")
            return
        }

        if bytesEqual(tokens[0], "fstress") {
            writeUserOutput("\r\n")
            if tokens.count < 5 {
                writeUserOutput("[fs] stress: usage fstress <path> <iters> <size> <safe|fast>\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] stress: invalid path\r\n")
                return
            }
            if norm.isRoot {
                writeUserOutput("[fs] stress: invalid path\r\n")
                return
            }
            let iters = parseU32(tokens[2])
            let size = parseU32(tokens[3])
            if iters == 0 || size == 0 {
                writeUserOutput("[fs] stress: invalid params\r\n")
                return
            }
            let policy: UInt32
            if bytesEqual(tokens[4], "safe") {
                policy = FAT32_POLICY_SAFE
            } else if bytesEqual(tokens[4], "fast") {
                policy = FAT32_POLICY_FAST
            } else {
                writeUserOutput("[fs] stress: invalid policy\r\n")
                return
            }
            runFsStressTest(path: norm, iterations: Int(iters), size: Int(size), policy: policy)
            return
        }

        if bytesEqual(tokens[0], "findkernel") {
            writeUserOutput("\r\n")
            if var task = gConsoleTask {
                locateKernelImage(task: &task)
                gConsoleTask = task
            }
            return
        }
    }
}



fileprivate func splitTokens(_ line: [UInt8]) -> [[UInt8]] {
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

fileprivate func parseContractRights(_ token: [UInt8]) -> UInt32? {
    if token.isEmpty { return nil }
    var rights: UInt32 = 0
    var start = 0
    var i = 0
    while i <= token.count {
        if i == token.count || token[i] == 0x7C { // '|'
            if start == i {
                return nil
            }
            let part = Array(token[start..<i])
            if bytesEqual(part, "read") {
                rights |= FAT32_CONTRACT_R_READ
            } else if bytesEqual(part, "list") {
                rights |= FAT32_CONTRACT_R_LIST
            } else if bytesEqual(part, "write") {
                rights |= FAT32_CONTRACT_R_WRITE
            } else {
                return nil
            }
            start = i + 1
        }
        i += 1
    }
    return rights == 0 ? nil : rights
}

fileprivate func parseU32(_ token: [UInt8]) -> UInt32 {
    if token.isEmpty { return 0 }
    var value: UInt32 = 0
    for b in token {
        if b < 0x30 || b > 0x39 { return 0 }
        let digit = UInt32(b - 0x30)
        value = value &* 10 &+ digit
    }
    return value
}
