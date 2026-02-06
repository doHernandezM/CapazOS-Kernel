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
            writeUserOutput("fsstats - show FS stats\r\n")
            writeUserOutput("fsstatsreset - reset FS stats\r\n")
            writeUserOutput("fstress <path> <iters> <size> <safe|fast> - FS stress test\r\n")
            writeUserOutput("qos <interactive|latency|throughput|background>\r\n")
            writeUserOutput("schedcontract [intent cpu readB writeB memB [window]]\r\n")
            writeUserOutput("schedstats - show scheduler/accounting stats\r\n")
            writeUserOutput("fat16cat <path> - read FAT16 file by path\r\n")
            writeUserOutput("fat16test <path> <size> <checksum> - FAT16 smoke test\r\n")
            writeUserOutput("fat32test <path> <size> <checksum> - FAT32 smoke test\r\n")
            writeUserOutput("fat16crash <path> <data> - simulate FAT16 crash recovery\r\n")
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
            let path: NormalizedPath
            if tokens.count >= 2 {
                guard let norm = normalizePath(tokens[1]) else {
                    writeUserOutput("[fs] ls: invalid path\r\n")
                    return
                }
                path = norm
            } else {
                path = NormalizedPath(flat: [0x2F], segments: [], isRoot: true)
            }
            guard let entries = fsList(path: path) else {
                writeUserOutput("[fs] ls: failed\r\n")
                return
            }
            for entry in entries {
                var line: [UInt8] = [0x20, 0x20]
                line.append(contentsOf: entry.name)
                if entry.isDir {
                    line.append(0x2F)
                }
                line.append(0x0D)
                line.append(0x0A)
                writeUserOutput(line)
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
            if let kind = fsDetectKind(), kind == .fat16 {
                writeUserOutput("[fs] open: not supported on FAT16\r\n")
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
            if let kind = fsDetectKind(), kind == .fat16 {
                writeUserOutput("[fs] openw: not supported on FAT16\r\n")
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
            if let data = fsRead(path: norm, intent: IO_INTENT_THROUGHPUT) {
                writeUserOutput(data)
                writeUserOutput("\r\n")
            } else {
                writeUserOutput("[fs] cat: read failed\r\n")
            }
            return
        }

        if bytesEqual(tokens[0], "fat16cat") {
            writeUserOutput("\r\n")
            if tokens.count < 2 {
                writeUserOutput("[fs] fat16cat: missing path\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] fat16cat: invalid path\r\n")
                return
            }
            if norm.isRoot {
                writeUserOutput("[fs] fat16cat: invalid path\r\n")
                return
            }
            if let data = fat16ReadFile(path: norm, intent: IO_INTENT_THROUGHPUT) {
                writeUserOutput(data)
                writeUserOutput("\r\n")
            } else {
                writeUserOutput("[fs] fat16cat: read failed\r\n")
            }
            return
        }

        if bytesEqual(tokens[0], "fat16test") {
            writeUserOutput("\r\n")
            if tokens.count < 4 {
                writeUserOutput("[fs] fat16test: usage fat16test <path> <size> <checksum>\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] fat16test: invalid path\r\n")
                return
            }
            let size = parseU32(tokens[2])
            let sum = parseU32(tokens[3])
            if size == 0 || sum == 0 {
                writeUserOutput("[fs] fat16test: invalid size/checksum\r\n")
                return
            }
            if fat16SmokeTest(path: norm, expectedSize: size, expectedChecksum: sum) {
                writeUserOutput("[fs] fat16test: ok\r\n")
            } else {
                writeUserOutput("[fs] fat16test: failed\r\n")
            }
            return
        }

        if bytesEqual(tokens[0], "fat16crash") {
            writeUserOutput("\r\n")
            if tokens.count < 3 {
                writeUserOutput("[fs] fat16crash: usage fat16crash <path> <data>\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] fat16crash: invalid path\r\n")
                return
            }
            let data = tokens[2]
            if fat16CrashTest(path: norm, data: data) {
                writeUserOutput("[fs] fat16crash: ok\r\n")
            } else {
                writeUserOutput("[fs] fat16crash: failed\r\n")
            }
            return
        }

        if bytesEqual(tokens[0], "fat32test") {
            writeUserOutput("\r\n")
            if tokens.count < 4 {
                writeUserOutput("[fs] fat32test: usage fat32test <path> <size> <checksum>\r\n")
                return
            }
            guard let norm = normalizePath(tokens[1]) else {
                writeUserOutput("[fs] fat32test: invalid path\r\n")
                return
            }
            let size = parseU32(tokens[2])
            let sum = parseU32(tokens[3])
            if size == 0 || sum == 0 {
                writeUserOutput("[fs] fat32test: invalid size/checksum\r\n")
                return
            }
            if fat32SmokeTest(path: norm, expectedSize: size, expectedChecksum: sum) {
                writeUserOutput("[fs] fat32test: ok\r\n")
            } else {
                writeUserOutput("[fs] fat32test: failed\r\n")
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
            let data = tokens[2]
            if fsWrite(path: norm, data: data, safe: true) {
                writeUserOutput("[fs] write ok\r\n")
            } else {
                writeUserOutput("[fs] write failed\r\n")
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
            let s = fsStatsSnapshot()
            writeUserOutput("[fs] stats\r\n")
            writeUserOutput(" reads=")
            writeUserOutput(u64ToDecBytes(s.readOps))
            writeUserOutput(" bytes=")
            writeUserOutput(u64ToDecBytes(s.readBytes))
            writeUserOutput(" (lat=")
            writeUserOutput(u64ToDecBytes(s.readLatencyOps))
            writeUserOutput(" thr=")
            writeUserOutput(u64ToDecBytes(s.readThroughputOps))
            writeUserOutput(" bg=")
            writeUserOutput(u64ToDecBytes(s.readBackgroundOps))
            writeUserOutput(")\r\n")
            writeUserOutput(" writes=")
            writeUserOutput(u64ToDecBytes(s.writeOps))
            writeUserOutput(" bytes=")
            writeUserOutput(u64ToDecBytes(s.writeBytes))
            writeUserOutput(" (lat=")
            writeUserOutput(u64ToDecBytes(s.writeLatencyOps))
            writeUserOutput(" thr=")
            writeUserOutput(u64ToDecBytes(s.writeThroughputOps))
            writeUserOutput(" bg=")
            writeUserOutput(u64ToDecBytes(s.writeBackgroundOps))
            writeUserOutput(")\r\n")
            writeUserOutput(" list=")
            writeUserOutput(u64ToDecBytes(s.listOps))
            writeUserOutput(" stat=")
            writeUserOutput(u64ToDecBytes(s.statOps))
            writeUserOutput("\r\n")
            return
        }

        if bytesEqual(tokens[0], "fsstatsreset") {
            writeUserOutput("\r\n")
            fsStatsReset()
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
            let safe: Bool
            if bytesEqual(tokens[4], "safe") {
                safe = true
            } else if bytesEqual(tokens[4], "fast") {
                safe = false
            } else {
                writeUserOutput("[fs] stress: invalid policy\r\n")
                return
            }
            if fsStressTest(path: norm, iterations: Int(iters), size: Int(size), safe: safe) {
                writeUserOutput("[fs] stress: ok\r\n")
            } else {
                writeUserOutput("[fs] stress: failed\r\n")
            }
            return
        }

        if bytesEqual(tokens[0], "qos") {
            writeUserOutput("\r\n")
            if tokens.count < 2 {
                writeUserOutput("[sched] qos: usage qos <interactive|latency|throughput|background>\r\n")
                return
            }
            guard let intent = parseSchedIntent(tokens[1]) else {
                writeUserOutput("[sched] qos: invalid intent\r\n")
                return
            }
            var contract = KsSchedContract(intent: intent,
                                           flags: 0,
                                           window_ticks: 100,
                                           cpu_ticks_limit: 85,
                                           io_read_bytes_limit: 4 * 1024 * 1024,
                                           io_write_bytes_limit: 2 * 1024 * 1024,
                                           mem_bytes_limit: 16 * 1024 * 1024)
            _ = cka_sched_get_contract(&contract)
            contract.intent = intent
            let st = withUnsafePointer(to: &contract) { ptr in
                cka_sched_set_contract(ptr)
            }
            _ = cka_sched_set_current_thread_intent(intent)
            if st == KS_STATUS_OK {
                writeUserOutput("[sched] qos updated\r\n")
            } else {
                writeUserOutput("[sched] qos update failed\r\n")
            }
            return
        }

        if bytesEqual(tokens[0], "schedcontract") {
            writeUserOutput("\r\n")
            var contract = KsSchedContract(intent: KS_SCHED_INTENT_INTERACTIVE,
                                           flags: 0,
                                           window_ticks: 100,
                                           cpu_ticks_limit: 0,
                                           io_read_bytes_limit: 0,
                                           io_write_bytes_limit: 0,
                                           mem_bytes_limit: 0)
            let getSt = cka_sched_get_contract(&contract)
            if getSt != KS_STATUS_OK {
                writeUserOutput("[sched] contract unavailable\r\n")
                return
            }

            if tokens.count > 1 {
                if tokens.count < 6 {
                    writeUserOutput("[sched] usage: schedcontract <intent> <cpuTicks> <readBytes> <writeBytes> <memBytes> [windowTicks]\r\n")
                    return
                }
                guard let intent = parseSchedIntent(tokens[1]) else {
                    writeUserOutput("[sched] invalid intent\r\n")
                    return
                }
                let cpuLimit = parseU64(tokens[2])
                let readLimit = parseU64(tokens[3])
                let writeLimit = parseU64(tokens[4])
                let memLimit = parseU64(tokens[5])
                let window = (tokens.count >= 7) ? parseU64(tokens[6]) : contract.window_ticks
                contract.intent = intent
                contract.cpu_ticks_limit = cpuLimit
                contract.io_read_bytes_limit = readLimit
                contract.io_write_bytes_limit = writeLimit
                contract.mem_bytes_limit = memLimit
                contract.window_ticks = window == 0 ? 100 : window
                let st = withUnsafePointer(to: &contract) { ptr in
                    cka_sched_set_contract(ptr)
                }
                _ = cka_sched_set_current_thread_intent(intent)
                if st != KS_STATUS_OK {
                    writeUserOutput("[sched] contract update failed\r\n")
                    return
                }
            }

            writeUserOutput("[sched] contract intent=")
            writeUserOutput(u64ToDecBytes(UInt64(contract.intent)))
            writeUserOutput(" window=")
            writeUserOutput(u64ToDecBytes(contract.window_ticks))
            writeUserOutput(" cpu=")
            writeUserOutput(u64ToDecBytes(contract.cpu_ticks_limit))
            writeUserOutput(" read=")
            writeUserOutput(u64ToDecBytes(contract.io_read_bytes_limit))
            writeUserOutput(" write=")
            writeUserOutput(u64ToDecBytes(contract.io_write_bytes_limit))
            writeUserOutput(" mem=")
            writeUserOutput(u64ToDecBytes(contract.mem_bytes_limit))
            writeUserOutput("\r\n")
            return
        }

        if bytesEqual(tokens[0], "schedstats") {
            writeUserOutput("\r\n")
            var stats = KsSchedStats(tick_now: 0,
                                     window_start_tick: 0,
                                     window_ticks: 0,
                                     cpu_ticks_total: 0,
                                     cpu_ticks_window: 0,
                                     cpu_throttle_events: 0,
                                     io_read_bytes_total: 0,
                                     io_read_bytes_window: 0,
                                     io_write_bytes_total: 0,
                                     io_write_bytes_window: 0,
                                     io_throttle_events: 0,
                                     mem_bytes_current: 0,
                                     mem_bytes_peak: 0,
                                     mem_throttle_events: 0,
                                     io_ops_total: 0,
                                     io_ops_interactive: 0,
                                     io_ops_latency: 0,
                                     io_ops_throughput: 0,
                                     io_ops_background: 0,
                                     sched_context_switches: 0,
                                     sched_yields: 0,
                                     sched_preempt_switches: 0,
                                     sched_runs_interactive: 0,
                                     sched_runs_latency: 0,
                                     sched_runs_throughput: 0,
                                     sched_runs_background: 0)
            let st = cka_sched_get_stats(&stats)
            if st != KS_STATUS_OK {
                writeUserOutput("[sched] stats unavailable\r\n")
                return
            }
            writeUserOutput("[sched] tick=")
            writeUserOutput(u64ToDecBytes(stats.tick_now))
            writeUserOutput(" window=")
            writeUserOutput(u64ToDecBytes(stats.window_ticks))
            writeUserOutput(" cpu=")
            writeUserOutput(u64ToDecBytes(stats.cpu_ticks_window))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.cpu_ticks_total))
            writeUserOutput("\r\n")
            writeUserOutput("[sched] io_r=")
            writeUserOutput(u64ToDecBytes(stats.io_read_bytes_window))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.io_read_bytes_total))
            writeUserOutput(" io_w=")
            writeUserOutput(u64ToDecBytes(stats.io_write_bytes_window))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.io_write_bytes_total))
            writeUserOutput("\r\n")
            writeUserOutput("[sched] mem=")
            writeUserOutput(u64ToDecBytes(stats.mem_bytes_current))
            writeUserOutput(" peak=")
            writeUserOutput(u64ToDecBytes(stats.mem_bytes_peak))
            writeUserOutput("\r\n")
            writeUserOutput("[sched] switches=")
            writeUserOutput(u64ToDecBytes(stats.sched_context_switches))
            writeUserOutput(" preempt=")
            writeUserOutput(u64ToDecBytes(stats.sched_preempt_switches))
            writeUserOutput(" yields=")
            writeUserOutput(u64ToDecBytes(stats.sched_yields))
            writeUserOutput("\r\n")
            writeUserOutput("[sched] runs i/l/t/b = ")
            writeUserOutput(u64ToDecBytes(stats.sched_runs_interactive))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.sched_runs_latency))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.sched_runs_throughput))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.sched_runs_background))
            writeUserOutput("\r\n")
            writeUserOutput("[sched] throttles cpu/io/mem = ")
            writeUserOutput(u64ToDecBytes(stats.cpu_throttle_events))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.io_throttle_events))
            writeUserOutput("/")
            writeUserOutput(u64ToDecBytes(stats.mem_throttle_events))
            writeUserOutput("\r\n")
            return
        }

        if bytesEqual(tokens[0], "findkernel") {
            writeUserOutput("\r\n")
            if var task = gConsoleTask {
                fsLocateKernelImage(task: &task)
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

fileprivate func parseU64(_ token: [UInt8]) -> UInt64 {
    if token.isEmpty { return 0 }
    var value: UInt64 = 0
    for b in token {
        if b < 0x30 || b > 0x39 { return 0 }
        let digit = UInt64(b - 0x30)
        value = value &* 10 &+ digit
    }
    return value
}

fileprivate func parseSchedIntent(_ token: [UInt8]) -> UInt32? {
    if bytesEqual(token, "interactive") {
        return KS_SCHED_INTENT_INTERACTIVE
    }
    if bytesEqual(token, "latency") {
        return KS_SCHED_INTENT_LATENCY
    }
    if bytesEqual(token, "throughput") {
        return KS_SCHED_INTENT_THROUGHPUT
    }
    if bytesEqual(token, "background") {
        return KS_SCHED_INTENT_BACKGROUND
    }
    return nil
}
