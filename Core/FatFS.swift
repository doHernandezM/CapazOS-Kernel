//
//  FatFS.swift
//  Core
//

func fsDetectKind(_ block: BlockDevice) -> FatType? {
    guard let boot0 = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if let bpb = fatParseBPB(boot0, baseLBA: 0) {
        return bpb.fatType
    }
    if let gptLBA = fatFindPartitionLBAFromGPT(block) {
        if gptLBA <= UInt64(UInt32.max),
           let boot = block.read(lba: gptLBA, count: 1, intent: IO_INTENT_THROUGHPUT),
           let bpb = fatParseBPB(boot, baseLBA: UInt32(gptLBA)) {
            return bpb.fatType
        }
    }
    if let partLBA = fatFindPartitionLBAFromMBR(boot0, types: [0x04, 0x06, 0x0E, 0x0B, 0x0C]) {
        if let boot = block.read(lba: UInt64(partLBA), count: 1, intent: IO_INTENT_THROUGHPUT),
           let bpb = fatParseBPB(boot, baseLBA: partLBA) {
            return bpb.fatType
        }
    }
    return nil
}

func fsDetectKind() -> FatType? {
    guard let block = gBlockDevice else { return nil }
    return fsDetectKind(block)
}

func fsLocateKernelImage(task: inout TaskContext) {
    if !task.canListRoot() || !task.hasContract(rights: FAT32_CONTRACT_R_LIST) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    guard let kind = fsDetectKind() else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no FAT volume\r\n")))
        return
    }
    let path: [UInt8]?
    if kind == .fat32 {
        path = fat32LocateKernelImage()
    } else {
        path = fat16LocateKernelImage()
    }
    if let p = path {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kernel at ")))
        gConsole?.writeUserOutput(p)
        gConsole?.writeUserOutput([0x0D, 0x0A])
    } else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kernel not found\r\n")))
    }
}

struct FsStat {
    let size: UInt32
    let isDir: Bool
}

struct FsStats {
    var readOps: UInt64 = 0
    var writeOps: UInt64 = 0
    var listOps: UInt64 = 0
    var statOps: UInt64 = 0
    var readBytes: UInt64 = 0
    var writeBytes: UInt64 = 0
    var readLatencyOps: UInt64 = 0
    var readThroughputOps: UInt64 = 0
    var readBackgroundOps: UInt64 = 0
    var writeLatencyOps: UInt64 = 0
    var writeThroughputOps: UInt64 = 0
    var writeBackgroundOps: UInt64 = 0
}

private var gFsStats = FsStats()

func fsStatsReset() {
    gFsStats = FsStats()
}

func fsStatsSnapshot() -> FsStats {
    return gFsStats
}

private func fsStatsRecordRead(bytes: Int, intent: UInt32) {
    gFsStats.readOps &+= 1
    gFsStats.readBytes &+= UInt64(bytes)
    if intent == IO_INTENT_THROUGHPUT {
        gFsStats.readThroughputOps &+= 1
    } else if intent == IO_INTENT_BACKGROUND {
        gFsStats.readBackgroundOps &+= 1
    } else {
        gFsStats.readLatencyOps &+= 1
    }
}

private func fsStatsRecordWrite(bytes: Int, intent: UInt32) {
    gFsStats.writeOps &+= 1
    gFsStats.writeBytes &+= UInt64(bytes)
    if intent == IO_INTENT_THROUGHPUT {
        gFsStats.writeThroughputOps &+= 1
    } else if intent == IO_INTENT_BACKGROUND {
        gFsStats.writeBackgroundOps &+= 1
    } else {
        gFsStats.writeLatencyOps &+= 1
    }
}

func fsRead(path: NormalizedPath, intent: UInt32) -> [UInt8]? {
    guard let kind = fsDetectKind() else { return nil }
    let data: [UInt8]?
    if kind == .fat32 {
        var task = TaskContext()
        let cap = issueFileCap(path: path.flat, rights: CAP_R_FILE_READ)
        if cap == 0 { return nil }
        task.addCap(cap)
        let contract = issueFat32Contract(rights: FAT32_CONTRACT_R_READ, policy: FAT32_POLICY_FAST)
        if contract != 0 {
            task.addContract(contract)
        }
        data = fat32ReadFileData(task: &task, path: path, intent: intent)
    } else {
        data = fat16ReadFile(path: path, intent: intent)
    }
    if let d = data {
        fsStatsRecordRead(bytes: d.count, intent: intent)
    }
    return data
}

func fsWrite(path: NormalizedPath, data: [UInt8], safe: Bool) -> Bool {
    guard let kind = fsDetectKind() else { return false }
    let ok: Bool
    if kind == .fat32 {
        var task = TaskContext()
        let cap = issueFileCap(path: path.flat, rights: safe ? CAP_R_FILE_WRITE_SAFE : CAP_R_FILE_WRITE_FAST)
        if cap == 0 { return false }
        task.addCap(cap)
        let policy = safe ? FAT32_POLICY_SAFE : FAT32_POLICY_FAST
        let contract = issueFat32Contract(rights: FAT32_CONTRACT_R_WRITE, policy: policy)
        if contract != 0 {
            task.addContract(contract)
        }
        ok = fat32WriteFileData(task: &task, path: path, data: data, safe: safe, emitOk: false)
    } else {
        ok = fat16WriteFile(path: path, data: data, safe: safe, simulateCrash: false)
    }
    if ok {
        fsStatsRecordWrite(bytes: data.count, intent: IO_INTENT_THROUGHPUT)
    }
    return ok
}

func fsList(path: NormalizedPath) -> [FatDirEntry]? {
    guard let kind = fsDetectKind() else { return nil }
    gFsStats.listOps &+= 1
    if kind == .fat32 {
        return fat32ListDirEntries(path: path)
    }
    return fat16ListDirEntries(path: path)
}

func fsStat(path: NormalizedPath) -> FsStat? {
    guard let kind = fsDetectKind() else { return nil }
    gFsStats.statOps &+= 1
    if kind == .fat32 {
        return fat32Stat(path: path)
    }
    return fat16Stat(path: path)
}

func fsDelete(path: NormalizedPath, safe: Bool) -> Bool {
    guard let kind = fsDetectKind() else { return false }
    if kind == .fat32 {
        return fat32DeleteFile(path: path, safe: safe)
    }
    return fat16DeleteFile(path: path, safe: safe)
}

func fsStressTest(path: NormalizedPath, iterations: Int, size: Int, safe: Bool) -> Bool {
    if iterations <= 0 || size <= 0 { return false }
    for i in 0..<iterations {
        var data = [UInt8](repeating: 0, count: size)
        for j in 0..<size {
            data[j] = UInt8(truncatingIfNeeded: j + i)
        }
        if !fsWrite(path: path, data: data, safe: safe) {
            return false
        }
        guard let readBack = fsRead(path: path, intent: IO_INTENT_LATENCY) else {
            return false
        }
        if readBack.count < size {
            return false
        }
        for j in 0..<size {
            if readBack[j] != data[j] {
                return false
            }
        }
        if !fsDelete(path: path, safe: safe) {
            return false
        }
    }
    return true
}
