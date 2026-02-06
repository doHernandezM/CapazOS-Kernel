//
//  Fat32.swift
//  Core
//

struct Fat32Stats {
    var mountCount: UInt64 = 0
    var dirListCount: UInt64 = 0
    var fileReadCount: UInt64 = 0
    var fileWriteCount: UInt64 = 0
    var readOps: UInt64 = 0
    var writeOps: UInt64 = 0
    var readBytes: UInt64 = 0
    var writeBytes: UInt64 = 0
    var readLatencyOps: UInt64 = 0
    var readThroughputOps: UInt64 = 0
    var readBackgroundOps: UInt64 = 0
    var writeLatencyOps: UInt64 = 0
    var writeThroughputOps: UInt64 = 0
    var writeBackgroundOps: UInt64 = 0
    var walWrites: UInt64 = 0
    var walReplays: UInt64 = 0
    var errorCount: UInt64 = 0
}

private var gFat32Stats = Fat32Stats()
private var gFat32FatCache: [UInt8] = []
private var gFat32FatCacheSig: UInt64 = 0
private var gFat32LoggedIoFailure = false

func fat32StatsReset() {
    gFat32Stats = Fat32Stats()
    gFat32FatCache.removeAll(keepingCapacity: false)
    gFat32FatCacheSig = 0
    gFat32LoggedIoFailure = false
}

func fat32StatsSnapshot() -> Fat32Stats {
    return gFat32Stats
}

func fat32StatsPrint() {
    let s = gFat32Stats
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stats\r\n")))
    gConsole?.writeUserOutput(Array(staticStringBytes(" mounts=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.mountCount))
    gConsole?.writeUserOutput(Array(staticStringBytes(" dirs=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.dirListCount))
    gConsole?.writeUserOutput(Array(staticStringBytes(" reads=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.fileReadCount))
    gConsole?.writeUserOutput(Array(staticStringBytes(" writes=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.fileWriteCount))
    gConsole?.writeUserOutput([0x0D, 0x0A])
    gConsole?.writeUserOutput(Array(staticStringBytes(" io reads=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.readOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(" bytes=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.readBytes))
    gConsole?.writeUserOutput(Array(staticStringBytes(" (lat=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.readLatencyOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(" thr=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.readThroughputOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(" bg=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.readBackgroundOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(")\r\n")))
    gConsole?.writeUserOutput(Array(staticStringBytes(" io writes=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.writeOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(" bytes=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.writeBytes))
    gConsole?.writeUserOutput(Array(staticStringBytes(" (lat=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.writeLatencyOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(" thr=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.writeThroughputOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(" bg=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.writeBackgroundOps))
    gConsole?.writeUserOutput(Array(staticStringBytes(")\r\n")))
    gConsole?.writeUserOutput(Array(staticStringBytes(" wal writes=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.walWrites))
    gConsole?.writeUserOutput(Array(staticStringBytes(" replays=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.walReplays))
    gConsole?.writeUserOutput(Array(staticStringBytes(" errors=")))
    gConsole?.writeUserOutput(u64ToDecBytes(s.errorCount))
    gConsole?.writeUserOutput([0x0D, 0x0A])
}

private func fat32StatsRecordError() {
    gFat32Stats.errorCount &+= 1
}

private func fat32LogIoFailureOnce(_ label: StaticString,
                                   lba: UInt64,
                                   count: UInt32,
                                   capacity: UInt64,
                                   vol: Fat32Volume?) {
    if gFat32LoggedIoFailure {
        return
    }
    gFat32LoggedIoFailure = true
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] io fail ")))
    gConsole?.writeUserOutput(Array(staticStringBytes(label)))
    gConsole?.writeUserOutput(Array(staticStringBytes(" lba=")))
    gConsole?.writeUserOutput(u64ToDecBytes(lba))
    gConsole?.writeUserOutput(Array(staticStringBytes(" count=")))
    gConsole?.writeUserOutput(u64ToDecBytes(UInt64(count)))
    gConsole?.writeUserOutput(Array(staticStringBytes(" cap=")))
    gConsole?.writeUserOutput(u64ToDecBytes(capacity))
    if let v = vol {
        gConsole?.writeUserOutput(Array(staticStringBytes(" base=")))
        gConsole?.writeUserOutput(u64ToDecBytes(UInt64(v.baseLBA)))
        gConsole?.writeUserOutput(Array(staticStringBytes(" total=")))
        gConsole?.writeUserOutput(u64ToDecBytes(UInt64(v.totalSectors)))
        gConsole?.writeUserOutput(Array(staticStringBytes(" fat=")))
        gConsole?.writeUserOutput(u64ToDecBytes(UInt64(v.fatStartLBA)))
        gConsole?.writeUserOutput(Array(staticStringBytes(" spf=")))
        gConsole?.writeUserOutput(u64ToDecBytes(UInt64(v.sectorsPerFAT)))
        gConsole?.writeUserOutput(Array(staticStringBytes(" data=")))
        gConsole?.writeUserOutput(u64ToDecBytes(UInt64(v.dataStartLBA)))
        gConsole?.writeUserOutput(Array(staticStringBytes(" spc=")))
        gConsole?.writeUserOutput(u64ToDecBytes(UInt64(v.sectorsPerCluster)))
    }
    gConsole?.writeUserOutput([0x0D, 0x0A])
}

private func fat32StatsRecordMount() {
    gFat32Stats.mountCount &+= 1
}

private func fat32StatsRecordDirList() {
    gFat32Stats.dirListCount &+= 1
}

private func fat32StatsRecordFileRead() {
    gFat32Stats.fileReadCount &+= 1
}

private func fat32StatsRecordFileWrite() {
    gFat32Stats.fileWriteCount &+= 1
}

private func fat32StatsRecordRead(bytes: Int, intent: UInt32) {
    gFat32Stats.readOps &+= 1
    gFat32Stats.readBytes &+= UInt64(bytes)
    if intent == IO_INTENT_THROUGHPUT {
        gFat32Stats.readThroughputOps &+= 1
    } else if intent == IO_INTENT_BACKGROUND {
        gFat32Stats.readBackgroundOps &+= 1
    } else {
        gFat32Stats.readLatencyOps &+= 1
    }
}

private func fat32StatsRecordWrite(bytes: Int, intent: UInt32) {
    gFat32Stats.writeOps &+= 1
    gFat32Stats.writeBytes &+= UInt64(bytes)
    if intent == IO_INTENT_THROUGHPUT {
        gFat32Stats.writeThroughputOps &+= 1
    } else if intent == IO_INTENT_BACKGROUND {
        gFat32Stats.writeBackgroundOps &+= 1
    } else {
        gFat32Stats.writeLatencyOps &+= 1
    }
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

public func toUpperASCII(_ b: UInt8) -> UInt8 {
    if b >= 0x61 && b <= 0x7A {
        return b - 0x20
    }
    return b
}

struct NormalizedPath {
    let flat: [UInt8]
    let segments: [[UInt8]]
    let isRoot: Bool
}

private func normalizeSegment(_ raw: [UInt8]) -> [UInt8]? {
    if raw.isEmpty { return nil }
    if raw.count == 1 && raw[0] == 0x2E { return nil } // "."
    if raw.count == 2 && raw[0] == 0x2E && raw[1] == 0x2E { return nil } // ".."
    var name: [UInt8] = []
    var ext: [UInt8] = []
    var seenDot = false
    for b in raw {
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

func normalizePath(_ raw: [UInt8]) -> NormalizedPath? {
    if raw.isEmpty { return nil }
    var start = 0
    var end = raw.count
    while start < end && raw[start] == 0x20 { start += 1 }
    while end > start && raw[end - 1] == 0x20 { end -= 1 }
    if start >= end { return nil }
    var trimmed = Array(raw[start..<end])
    if trimmed.count == 1 && trimmed[0] == 0x2F {
        return NormalizedPath(flat: [0x2F], segments: [], isRoot: true)
    }
    if trimmed.first == 0x2F {
        trimmed.removeFirst()
    }
    var segments: [[UInt8]] = []
    var current: [UInt8] = []
    for b in trimmed {
        if b == 0x5C || b == 0x3A { return nil } // '\' or ':'
        if b == 0x2F {
            if current.isEmpty { return nil }
            guard let norm = normalizeSegment(current) else { return nil }
            segments.append(norm)
            current = []
        } else {
            current.append(b)
        }
    }
    if !current.isEmpty {
        guard let norm = normalizeSegment(current) else { return nil }
        segments.append(norm)
    }
    if segments.isEmpty { return nil }
    var flat: [UInt8] = []
    for i in 0..<segments.count {
        if i > 0 { flat.append(0x2F) }
        flat.append(contentsOf: segments[i])
    }
    return NormalizedPath(flat: flat, segments: segments, isRoot: false)
}

struct BlockDevice {
    let sectorSize: UInt32
    let capacitySectors: UInt64
    
    func read(lba: UInt64, count: UInt32, intent: UInt32) -> [UInt8]? {
        if count == 0 {
            return []
        }
        let cap = capacitySectors
        if lba + UInt64(count) > cap {
            fat32StatsRecordError()
            fat32LogIoFailureOnce("range", lba: lba, count: count, capacity: cap, vol: nil)
            return nil
        }
        let byteCount = Int(count) * Int(sectorSize)
        var buf = [UInt8](repeating: 0, count: byteCount)
        let st = buf.withUnsafeMutableBytes { raw -> Int32 in
            guard let base = raw.baseAddress else { return KS_STATUS_INVALID_ARG }
            return cka_block_read_intent(lba, count, base, byteCount, intent)
        }
        if st != KS_STATUS_OK {
            fat32StatsRecordError()
            fat32LogIoFailureOnce("read", lba: lba, count: count, capacity: cap, vol: nil)
            return nil
        }
        fat32StatsRecordRead(bytes: byteCount, intent: intent)
        return buf
    }

    func write(lba: UInt64, count: UInt32, data: [UInt8], intent: UInt32) -> Bool {
        if count == 0 {
            return true
        }
        let cap = capacitySectors
        if lba + UInt64(count) > cap {
            fat32StatsRecordError()
            fat32LogIoFailureOnce("range", lba: lba, count: count, capacity: cap, vol: nil)
            return false
        }
        let byteCount = Int(count) * Int(sectorSize)
        if data.count < byteCount {
            return false
        }
        let st = data.withUnsafeBytes { raw -> Int32 in
            guard let base = raw.baseAddress else { return KS_STATUS_INVALID_ARG }
            return cka_block_write_intent(lba, count, base, byteCount, intent)
        }
        if st != KS_STATUS_OK {
            fat32StatsRecordError()
            fat32LogIoFailureOnce("write", lba: lba, count: count, capacity: cap, vol: nil)
            return false
        }
        fat32StatsRecordWrite(bytes: byteCount, intent: intent)
        return true
    }
}

private struct Fat32Volume {
    let bytesPerSector: UInt16
    let sectorsPerCluster: UInt8
    let reservedSectors: UInt16
    let numFATs: UInt8
    let sectorsPerFAT: UInt32
    let totalSectors: UInt32
    let rootCluster: UInt32
    let baseLBA: UInt32
    let fatStartLBA: UInt32
    let dataStartLBA: UInt32
    let walStartLBA: UInt32
    let walSectors: UInt32
}

private func parseFat32BootSector(_ data: [UInt8], baseLBA: UInt32) -> Fat32Volume? {
    guard let bpb = fatParseBPB(data, baseLBA: baseLBA) else { return nil }
    if bpb.fatType != .fat32 { return nil }
    return Fat32Volume(bytesPerSector: bpb.bytesPerSector,
                       sectorsPerCluster: bpb.sectorsPerCluster,
                       reservedSectors: bpb.reservedSectors,
                       numFATs: bpb.numFATs,
                       sectorsPerFAT: bpb.sectorsPerFAT,
                       totalSectors: bpb.totalSectors,
                       rootCluster: bpb.rootCluster,
                       baseLBA: bpb.baseLBA,
                       fatStartLBA: bpb.fatStartLBA,
                       dataStartLBA: bpb.dataStartLBA,
                       walStartLBA: 0,
                       walSectors: 0)
}


private func configureWalRegion(_ vol: Fat32Volume) -> Fat32Volume {
    let walSectors: UInt32 = 2
    if vol.reservedSectors < 16 || UInt32(vol.reservedSectors) <= walSectors + 8 {
        return Fat32Volume(bytesPerSector: vol.bytesPerSector,
                           sectorsPerCluster: vol.sectorsPerCluster,
                           reservedSectors: vol.reservedSectors,
                           numFATs: vol.numFATs,
                           sectorsPerFAT: vol.sectorsPerFAT,
                           totalSectors: vol.totalSectors,
                           rootCluster: vol.rootCluster,
                           baseLBA: vol.baseLBA,
                           fatStartLBA: vol.fatStartLBA,
                           dataStartLBA: vol.dataStartLBA,
                           walStartLBA: 0,
                           walSectors: 0)
    }
    let walStart = vol.baseLBA + UInt32(vol.reservedSectors) - walSectors
    if walStart <= vol.baseLBA + 8 {
        return Fat32Volume(bytesPerSector: vol.bytesPerSector,
                           sectorsPerCluster: vol.sectorsPerCluster,
                           reservedSectors: vol.reservedSectors,
                           numFATs: vol.numFATs,
                           sectorsPerFAT: vol.sectorsPerFAT,
                           totalSectors: vol.totalSectors,
                           rootCluster: vol.rootCluster,
                           baseLBA: vol.baseLBA,
                           fatStartLBA: vol.fatStartLBA,
                           dataStartLBA: vol.dataStartLBA,
                           walStartLBA: 0,
                           walSectors: 0)
    }
    return Fat32Volume(bytesPerSector: vol.bytesPerSector,
                       sectorsPerCluster: vol.sectorsPerCluster,
                       reservedSectors: vol.reservedSectors,
                       numFATs: vol.numFATs,
                       sectorsPerFAT: vol.sectorsPerFAT,
                       totalSectors: vol.totalSectors,
                       rootCluster: vol.rootCluster,
                       baseLBA: vol.baseLBA,
                       fatStartLBA: vol.fatStartLBA,
                       dataStartLBA: vol.dataStartLBA,
                       walStartLBA: walStart,
                       walSectors: walSectors)
}

private func walReplayIfNeeded(_ block: BlockDevice, _ vol: Fat32Volume) {
    if vol.walStartLBA == 0 || vol.walSectors < 2 {
        return
    }
    guard let header = block.read(lba: UInt64(vol.walStartLBA), count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return
    }
    if header.count < 32 { return }
    if header[0] != 0x57 || header[1] != 0x41 || header[2] != 0x4C || header[3] != 0x31 {
        return
    }
    let state = readU32LE(header, 4)
    if state != 1 {
        return
    }
    let targetLBA = readU64LE(header, 8)
    let count = readU32LE(header, 16)
    let dataLen = readU32LE(header, 20)
    if count != 1 || dataLen == 0 || dataLen > vol.bytesPerSector {
        return
    }
    guard let data = block.read(lba: UInt64(vol.walStartLBA + 1), count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return
    }
    if data.count < Int(vol.bytesPerSector) { return }
    if targetLBA == 0 { return }
    _ = block.write(lba: targetLBA, count: 1, data: data, intent: IO_INTENT_THROUGHPUT)
    gFat32Stats.walReplays &+= 1
    var clear = [UInt8](repeating: 0, count: Int(vol.bytesPerSector))
    _ = block.write(lba: UInt64(vol.walStartLBA), count: 1, data: clear, intent: IO_INTENT_THROUGHPUT)
}

private func walWriteSector(_ block: BlockDevice, _ vol: Fat32Volume, targetLBA: UInt64, data: [UInt8]) -> Bool {
    if vol.walStartLBA == 0 || vol.walSectors < 2 {
        return false
    }
    let sectorSize = Int(vol.bytesPerSector)
    if data.count < sectorSize {
        return false
    }
    var header = [UInt8](repeating: 0, count: sectorSize)
    header[0] = 0x57 // W
    header[1] = 0x41 // A
    header[2] = 0x4C // L
    header[3] = 0x31 // 1
    writeU32LE(1, &header, 4) // state=prepared
    var t = targetLBA
    for i in 0..<8 {
        header[8 + i] = UInt8(truncatingIfNeeded: t >> (UInt64(i) * 8))
    }
    writeU32LE(1, &header, 16) // count=1
    writeU32LE(UInt32(sectorSize), &header, 20)
    if !block.write(lba: UInt64(vol.walStartLBA), count: 1, data: header, intent: IO_INTENT_THROUGHPUT) {
        return false
    }
    if !block.write(lba: UInt64(vol.walStartLBA + 1), count: 1, data: data, intent: IO_INTENT_THROUGHPUT) {
        return false
    }
    if !block.write(lba: targetLBA, count: 1, data: data, intent: IO_INTENT_THROUGHPUT) {
        return false
    }
    var clear = [UInt8](repeating: 0, count: sectorSize)
    _ = block.write(lba: UInt64(vol.walStartLBA), count: 1, data: clear, intent: IO_INTENT_THROUGHPUT)
    gFat32Stats.walWrites &+= 1
    return true
}

private func mountFat32(_ block: BlockDevice) -> Fat32Volume? {
    fat32StatsRecordMount()
    guard let boot0 = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if let vol = parseFat32BootSector(boot0, baseLBA: 0) {
        let out = configureWalRegion(vol)
        if !validateFat32Volume(block, out) { return nil }
        walReplayIfNeeded(block, out)
        return out
    }
    if let gptLBA = fatFindPartitionLBAFromGPT(block) {
        if gptLBA <= UInt64(UInt32.max),
           let boot = block.read(lba: gptLBA, count: 1, intent: IO_INTENT_THROUGHPUT) {
            if let vol = parseFat32BootSector(boot, baseLBA: UInt32(gptLBA)) {
                let out = configureWalRegion(vol)
                if !validateFat32Volume(block, out) { return nil }
                walReplayIfNeeded(block, out)
                return out
            }
        }
    }
    guard let partLBA = fatFindPartitionLBAFromMBR(boot0, types: [0x0B, 0x0C]) else {
        return nil
    }
    guard let boot = block.read(lba: UInt64(partLBA), count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if let vol = parseFat32BootSector(boot, baseLBA: partLBA) {
        let out = configureWalRegion(vol)
        if !validateFat32Volume(block, out) { return nil }
        walReplayIfNeeded(block, out)
        return out
    }
    return nil
}

private func validateFat32Volume(_ block: BlockDevice, _ vol: Fat32Volume) -> Bool {
    if vol.bytesPerSector == 0 || vol.sectorsPerCluster == 0 || vol.sectorsPerFAT == 0 {
        fat32StatsRecordError()
        return false
    }
    if UInt32(block.sectorSize) != UInt32(vol.bytesPerSector) {
        fat32StatsRecordError()
        return false
    }
    let cap = UInt64(block.capacitySectors)
    let base = UInt64(vol.baseLBA)
    let total = UInt64(vol.totalSectors)
    if total == 0 || base + total > cap {
        fat32StatsRecordError()
        return false
    }
    let fatStart = UInt64(vol.fatStartLBA)
    let fatEnd = fatStart + UInt64(vol.sectorsPerFAT) * UInt64(vol.numFATs)
    let dataStart = UInt64(vol.dataStartLBA)
    if fatStart < base || fatEnd > base + total || dataStart < base || dataStart >= base + total {
        fat32StatsRecordError()
        return false
    }
    return true
}

public func trimSpaces(_ bytes: [UInt8]) -> [UInt8] {
    var start = 0
    var end = bytes.count
    while start < end && bytes[start] == 0x20 { start += 1 }
    while end > start && bytes[end - 1] == 0x20 { end -= 1 }
    if start >= end { return [] }
    return Array(bytes[start..<end])
}

private func readFat32FAT(_ block: BlockDevice, _ vol: Fat32Volume) -> [UInt8]? {
    if vol.sectorsPerFAT == 0 { return nil }
    let cap = UInt64(block.capacitySectors)
    let fatStart = UInt64(vol.fatStartLBA)
    let fatSectors = UInt64(vol.sectorsPerFAT)
    if fatStart + fatSectors > cap {
        fat32StatsRecordError()
        fat32LogIoFailureOnce("fat-range", lba: fatStart, count: vol.sectorsPerFAT, capacity: cap, vol: vol)
        return nil
    }
    let sig = fat32VolumeSig(vol)
    let expectedBytes = Int(vol.sectorsPerFAT) * Int(vol.bytesPerSector)
    if gFat32FatCacheSig != sig || gFat32FatCache.count != expectedBytes {
        gFat32FatCache = [UInt8](repeating: 0, count: expectedBytes)
        gFat32FatCacheSig = sig
    }
    var offset = 0
    for i in 0..<vol.sectorsPerFAT {
        if let part = block.read(lba: UInt64(vol.fatStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            if part.count != Int(vol.bytesPerSector) {
                fat32StatsRecordError()
                fat32LogIoFailureOnce("fat-short", lba: UInt64(vol.fatStartLBA + i), count: 1, capacity: cap, vol: vol)
                return nil
            }
            gFat32FatCache.replaceSubrange(offset..<(offset + part.count), with: part)
            offset += part.count
        } else {
            fat32StatsRecordError()
            fat32LogIoFailureOnce("fat-read", lba: UInt64(vol.fatStartLBA + i), count: 1, capacity: cap, vol: vol)
            return nil
        }
    }
    return gFat32FatCache
}

private func fat32VolumeSig(_ vol: Fat32Volume) -> UInt64 {
    var sig: UInt64 = 0
    sig ^= UInt64(vol.baseLBA)
    sig = (sig << 16) ^ UInt64(vol.sectorsPerFAT)
    sig = (sig << 8) ^ UInt64(vol.numFATs)
    sig = (sig << 16) ^ UInt64(vol.dataStartLBA)
    sig = (sig << 16) ^ UInt64(vol.totalSectors)
    return sig
}

private func fat32MaxCluster(_ vol: Fat32Volume) -> UInt32 {
    let dataSectors = Int64(vol.totalSectors) - Int64(vol.dataStartLBA - vol.baseLBA)
    if dataSectors <= 0 { return 0 }
    let clusters = UInt64(dataSectors) / UInt64(vol.sectorsPerCluster)
    if clusters == 0 { return 0 }
    return UInt32(clusters + 1)
}

private func fat32NextCluster(_ fat: [UInt8], _ cluster: UInt32) -> UInt32 {
    return fatNextCluster(fat, fatType: .fat32, cluster: cluster)
}

private func fat32SetCluster(_ fat: inout [UInt8], _ cluster: UInt32, _ value: UInt32) {
    let off = Int(cluster * 4)
    if off + 3 >= fat.count { return }
    let old = readU32LE(fat, off)
    let merged = (old & 0xF0000000) | (value & 0x0FFFFFFF)
    writeU32LE(merged, &fat, off)
}

private func fat32ClusterChain(_ vol: Fat32Volume, _ fat: [UInt8], _ start: UInt32) -> [UInt32] {
    let maxCluster = fat32MaxCluster(vol)
    return fatClusterChain(fatType: .fat32, fat: fat, start: start, maxCluster: maxCluster) ?? []
}

private func fat32FindFreeClusters(_ vol: Fat32Volume, _ fat: [UInt8], count: Int) -> [UInt32]? {
    if count <= 0 { return [] }
    var result: [UInt32] = []
    let maxCluster = fat32MaxCluster(vol)
    if maxCluster < 2 { return nil }
    for c in 2...Int(maxCluster) {
        let cluster = UInt32(c)
        let val = fat32NextCluster(fat, cluster)
        if val == 0 {
            result.append(cluster)
            if result.count >= count {
                return result
            }
        }
    }
    return nil
}

private func writeFatTables(_ block: BlockDevice, _ vol: Fat32Volume, _ fat: [UInt8], safe: Bool) -> Bool {
    let sectorSize = Int(vol.bytesPerSector)
    let fatSectors = Int(vol.sectorsPerFAT)
    let fatBytes = fatSectors * sectorSize
    if fatBytes == 0 || fat.count < fatBytes {
        return false
    }
    for fatIndex in 0..<Int(vol.numFATs) {
        let base = UInt64(vol.fatStartLBA) + UInt64(fatIndex * Int(vol.sectorsPerFAT))
        for s in 0..<fatSectors {
            let off = s * sectorSize
            let sectorData = Array(fat[off..<(off + sectorSize)])
            let lba = base + UInt64(s)
            if safe {
                if !walWriteSector(block, vol, targetLBA: lba, data: sectorData) {
                    return false
                }
            } else {
                if !block.write(lba: lba, count: 1, data: sectorData, intent: IO_INTENT_THROUGHPUT) {
                    return false
                }
            }
        }
    }
    return true
}

private func readCluster(_ block: BlockDevice, _ vol: Fat32Volume, _ cluster: UInt32, intent: UInt32) -> [UInt8]? {
    let maxCluster = fat32MaxCluster(vol)
    if cluster < 2 || cluster > maxCluster {
        fat32StatsRecordError()
        fat32LogIoFailureOnce("cluster", lba: UInt64(cluster), count: 0, capacity: UInt64(block.capacitySectors), vol: vol)
        return nil
    }
    let dataLBA = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
    let cap = UInt64(block.capacitySectors)
    let end = dataLBA + UInt64(vol.sectorsPerCluster)
    if end > cap {
        fat32StatsRecordError()
        fat32LogIoFailureOnce("cluster-range", lba: dataLBA, count: UInt32(vol.sectorsPerCluster), capacity: cap, vol: vol)
        return nil
    }
    var out: [UInt8] = []
    for s in 0..<UInt32(vol.sectorsPerCluster) {
        if let part = block.read(lba: dataLBA + UInt64(s), count: 1, intent: intent) {
            out.append(contentsOf: part)
        } else {
            fat32StatsRecordError()
            fat32LogIoFailureOnce("cluster-read", lba: dataLBA + UInt64(s), count: 1, capacity: cap, vol: vol)
            return nil
        }
    }
    return out
}

private func writeCluster(_ block: BlockDevice, _ vol: Fat32Volume, _ cluster: UInt32, data: [UInt8], safe: Bool) -> Bool {
    let maxCluster = fat32MaxCluster(vol)
    if cluster < 2 || cluster > maxCluster {
        fat32StatsRecordError()
        fat32LogIoFailureOnce("cluster", lba: UInt64(cluster), count: 0, capacity: UInt64(block.capacitySectors), vol: vol)
        return false
    }
    let dataLBA = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
    let cap = UInt64(block.capacitySectors)
    let end = dataLBA + UInt64(vol.sectorsPerCluster)
    if end > cap {
        fat32StatsRecordError()
        fat32LogIoFailureOnce("cluster-range", lba: dataLBA, count: UInt32(vol.sectorsPerCluster), capacity: cap, vol: vol)
        return false
    }
    let sectorSize = Int(vol.bytesPerSector)
    if data.count < sectorSize * Int(vol.sectorsPerCluster) {
        return false
    }
    for s in 0..<UInt32(vol.sectorsPerCluster) {
        let off = Int(s) * sectorSize
        let sectorData = Array(data[off..<(off + sectorSize)])
        if safe {
            if !walWriteSector(block, vol, targetLBA: dataLBA + UInt64(s), data: sectorData) {
                return false
            }
        } else {
            if !block.write(lba: dataLBA + UInt64(s), count: 1, data: sectorData, intent: IO_INTENT_THROUGHPUT) {
                return false
            }
        }
    }
    return true
}

private typealias Fat32DirEntry = FatDirEntry

private struct DirEntryLocation {
    let cluster: UInt32
    let entryOffset: Int
}

private func findEntryLocationInDir(_ block: BlockDevice, _ vol: Fat32Volume, _ fat: [UInt8], dirCluster: UInt32, name: [UInt8]) -> (Fat32DirEntry, DirEntryLocation)? {
    let clusterSize = Int(UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster))
    var cluster = dirCluster
    var guardCount: UInt32 = 0
    while cluster >= 2 && guardCount < 0x100000 {
        guard let data = readCluster(block, vol, cluster, intent: IO_INTENT_THROUGHPUT) else {
            return nil
        }
        if data.count < clusterSize {
            return nil
        }
        let records = fatScanDirEntries(data, fatType: .fat32, includeLFN: true)
        for rec in records {
            if rec.entry.name == name {
                return (rec.entry, DirEntryLocation(cluster: cluster, entryOffset: rec.offset))
            }
        }
        let next = fat32NextCluster(fat, cluster)
        if next >= 0x0FFFFFF8 || next == 0x0FFFFFF7 {
            break
        }
        cluster = next
        guardCount &+= 1
    }
    return nil
}

private func resolvePathEntryLocation(_ block: BlockDevice, _ vol: Fat32Volume, _ fat: [UInt8], _ path: NormalizedPath) -> (Fat32DirEntry, DirEntryLocation)? {
    if path.isRoot || path.segments.isEmpty {
        return nil
    }
    var dirCluster = vol.rootCluster
    for i in 0..<path.segments.count {
        if i == path.segments.count - 1 {
            return findEntryLocationInDir(block, vol, fat, dirCluster: dirCluster, name: path.segments[i])
        }
        guard let entry = findEntryInDir(block, vol, fat, dirCluster: dirCluster, name: path.segments[i]) else {
            return nil
        }
        if !entry.isDir || entry.firstCluster < 2 {
            return nil
        }
        dirCluster = entry.firstCluster
    }
    return nil
}

private func readDirEntries(_ block: BlockDevice, _ vol: Fat32Volume, _ fat: [UInt8], startCluster: UInt32, intent: UInt32) -> [Fat32DirEntry]? {
    var entries: [Fat32DirEntry] = []
    var cluster = startCluster
    var guardCount: UInt32 = 0
    while cluster >= 2 && guardCount < 0x100000 {
        guard let data = readCluster(block, vol, cluster, intent: intent) else {
            return nil
        }
        let records = fatScanDirEntries(data, fatType: .fat32, includeLFN: true)
        for rec in records {
            entries.append(rec.entry)
        }
        let next = fat32NextCluster(fat, cluster)
        if next >= 0x0FFFFFF8 || next == 0x0FFFFFF7 {
            break
        }
        cluster = next
        guardCount &+= 1
    }
    return entries
}

private func updateDirEntryOnDisk(_ block: BlockDevice, _ vol: Fat32Volume, _ loc: DirEntryLocation, newFirstCluster: UInt32, newSize: UInt32, safe: Bool) -> Bool {
    let sectorSize = Int(vol.bytesPerSector)
    let clusterSize = sectorSize * Int(vol.sectorsPerCluster)
    if loc.entryOffset < 0 || loc.entryOffset + 32 > clusterSize {
        return false
    }
    let sectorIndex = loc.entryOffset / sectorSize
    let offsetInSector = loc.entryOffset % sectorSize
    let lba = UInt64(vol.dataStartLBA) + UInt64(loc.cluster - 2) * UInt64(vol.sectorsPerCluster) + UInt64(sectorIndex)
    guard var sector = block.read(lba: lba, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return false
    }
    if sector.count < sectorSize { return false }
    let hi = UInt16(truncatingIfNeeded: (newFirstCluster >> 16) & 0xFFFF)
    let lo = UInt16(truncatingIfNeeded: newFirstCluster & 0xFFFF)
    writeU16LE(hi, &sector, offsetInSector + 20)
    writeU16LE(lo, &sector, offsetInSector + 26)
    writeU32LE(newSize, &sector, offsetInSector + 28)
    if safe {
        return walWriteSector(block, vol, targetLBA: lba, data: sector)
    }
    return block.write(lba: lba, count: 1, data: sector, intent: IO_INTENT_THROUGHPUT)
}

private func markDirEntryDeleted(_ block: BlockDevice, _ vol: Fat32Volume, _ loc: DirEntryLocation, safe: Bool) -> Bool {
    let sectorSize = Int(vol.bytesPerSector)
    let clusterSize = sectorSize * Int(vol.sectorsPerCluster)
    if loc.entryOffset < 0 || loc.entryOffset + 1 > clusterSize {
        return false
    }
    let sectorIndex = loc.entryOffset / sectorSize
    let offsetInSector = loc.entryOffset % sectorSize
    let lba = UInt64(vol.dataStartLBA) + UInt64(loc.cluster - 2) * UInt64(vol.sectorsPerCluster) + UInt64(sectorIndex)
    guard var sector = block.read(lba: lba, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return false
    }
    if sector.count < sectorSize { return false }
    sector[offsetInSector] = 0xE5
    if safe {
        return walWriteSector(block, vol, targetLBA: lba, data: sector)
    }
    return block.write(lba: lba, count: 1, data: sector, intent: IO_INTENT_THROUGHPUT)
}

private func findEntryInDir(_ block: BlockDevice, _ vol: Fat32Volume, _ fat: [UInt8], dirCluster: UInt32, name: [UInt8]) -> Fat32DirEntry? {
    guard let entries = readDirEntries(block, vol, fat, startCluster: dirCluster, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    for entry in entries {
        if entry.name == name {
            return entry
        }
    }
    return nil
}

private func resolvePathEntry(_ block: BlockDevice, _ vol: Fat32Volume, _ fat: [UInt8], _ path: NormalizedPath) -> Fat32DirEntry? {
    if path.isRoot || path.segments.isEmpty {
        return Fat32DirEntry(name: [0x2F], attr: 0x10, firstCluster: vol.rootCluster, fileSize: 0)
    }
    var dirCluster = vol.rootCluster
    for i in 0..<path.segments.count {
        guard let entry = findEntryInDir(block, vol, fat, dirCluster: dirCluster, name: path.segments[i]) else {
            return nil
        }
        if i == path.segments.count - 1 {
            return entry
        }
        if !entry.isDir {
            return nil
        }
        if entry.firstCluster < 2 {
            return nil
        }
        dirCluster = entry.firstCluster
    }
    return nil
}

func listFat32Directory(task: inout TaskContext, path: NormalizedPath) {
    let capPath: [UInt8] = path.isRoot ? [0x2F] : path.flat
    if !task.canListDir(path: capPath) || !task.hasContract(rights: FAT32_CONTRACT_R_LIST) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        fat32StatsRecordError()
        return
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        fat32StatsRecordError()
        return
    }
    guard let vol = mountFat32(block) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT32 volume\r\n")))
        fat32StatsRecordError()
        return
    }
    guard let fat = readFat32FAT(block, vol) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
        fat32StatsRecordError()
        return
    }
    let entry: Fat32DirEntry
    if path.isRoot {
        entry = Fat32DirEntry(name: [0x2F], attr: 0x10, firstCluster: vol.rootCluster, fileSize: 0)
    } else {
        guard let found = resolvePathEntry(block, vol, fat, path) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] path not found\r\n")))
            fat32StatsRecordError()
            return
        }
        entry = found
    }
    if !entry.isDir {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a directory\r\n")))
        fat32StatsRecordError()
        return
    }
    fat32StatsRecordDirList()
    let listIntent: UInt32
    if task.contractPolicy(rights: FAT32_CONTRACT_R_LIST) == FAT32_POLICY_BACKGROUND {
        listIntent = IO_INTENT_BACKGROUND
    } else {
        listIntent = IO_INTENT_THROUGHPUT
    }
    guard let entries = readDirEntries(block, vol, fat, startCluster: entry.firstCluster, intent: listIntent) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read directory\r\n")))
        fat32StatsRecordError()
        return
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("FAT32 dir:\r\n")))
    for entry in entries {
        var line: [UInt8] = [0x20, 0x20]
        line.append(contentsOf: entry.name)
        if entry.isDir {
            line.append(0x2F)
        }
        line.append(0x0D)
        line.append(0x0A)
        gConsole?.writeUserOutput(line)
    }
}

func fat32ListDirEntries(path: NormalizedPath) -> [FatDirEntry]? {
    guard let block = gBlockDevice else { return nil }
    guard let vol = mountFat32(block) else { return nil }
    guard let fat = readFat32FAT(block, vol) else { return nil }
    let entry: FatDirEntry
    if path.isRoot {
        entry = FatDirEntry(name: [0x2F], attr: 0x10, firstCluster: vol.rootCluster, fileSize: 0)
    } else {
        guard let found = resolvePathEntry(block, vol, fat, path) else { return nil }
        entry = found
    }
    if !entry.isDir { return nil }
    return readDirEntries(block, vol, fat, startCluster: entry.firstCluster, intent: IO_INTENT_THROUGHPUT)
}

func fat32Stat(path: NormalizedPath) -> FsStat? {
    guard let block = gBlockDevice else { return nil }
    guard let vol = mountFat32(block) else { return nil }
    guard let fat = readFat32FAT(block, vol) else { return nil }
    if path.isRoot {
        return FsStat(size: 0, isDir: true)
    }
    guard let entry = resolvePathEntry(block, vol, fat, path) else { return nil }
    return FsStat(size: entry.fileSize, isDir: entry.isDir)
}

func locateKernelImage(task: inout TaskContext) {
    if !task.canListRoot() || !task.hasContract(rights: FAT32_CONTRACT_R_LIST) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return
    }
    guard let vol = mountFat32(block) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT32 volume\r\n")))
        return
    }
    guard let fat = readFat32FAT(block, vol) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
        return
    }
    let candidates: [[UInt8]] = [
        Array(staticStringBytes("KERNEL8.IMG")),
        Array(staticStringBytes("KERNEL7L.IMG")),
        Array(staticStringBytes("KERNEL7.IMG")),
        Array(staticStringBytes("KERNEL.IMG"))
    ]
    if let rootEntries = readDirEntries(block, vol, fat, startCluster: vol.rootCluster, intent: IO_INTENT_THROUGHPUT) {
        if let found = findKernelName(in: rootEntries, candidates: candidates) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kernel at ")))
            gConsole?.writeUserOutput(found)
            gConsole?.writeUserOutput([0x0D, 0x0A])
            return
        }
        let bootName = Array(staticStringBytes("BOOT"))
        if let bootEntry = findEntryInDir(block, vol, fat, dirCluster: vol.rootCluster, name: bootName),
           bootEntry.isDir,
           bootEntry.firstCluster >= 2,
           let bootEntries = readDirEntries(block, vol, fat, startCluster: bootEntry.firstCluster, intent: IO_INTENT_THROUGHPUT),
           let found = findKernelName(in: bootEntries, candidates: candidates) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kernel at ")))
            gConsole?.writeUserOutput(bootName)
            gConsole?.writeUserOutput([0x2F])
            gConsole?.writeUserOutput(found)
            gConsole?.writeUserOutput([0x0D, 0x0A])
            return
        }
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kernel not found\r\n")))
}

func fat32LocateKernelImage() -> [UInt8]? {
    guard let block = gBlockDevice else { return nil }
    guard let vol = mountFat32(block) else { return nil }
    guard let fat = readFat32FAT(block, vol) else { return nil }
    let candidates: [[UInt8]] = [
        Array(staticStringBytes("KERNEL8.IMG")),
        Array(staticStringBytes("KERNEL7L.IMG")),
        Array(staticStringBytes("KERNEL7.IMG")),
        Array(staticStringBytes("KERNEL.IMG"))
    ]
    if let rootEntries = readDirEntries(block, vol, fat, startCluster: vol.rootCluster, intent: IO_INTENT_THROUGHPUT) {
        if let found = findKernelName(in: rootEntries, candidates: candidates) {
            return found
        }
        let bootName = Array(staticStringBytes("BOOT"))
        if let bootEntry = findEntryInDir(block, vol, fat, dirCluster: vol.rootCluster, name: bootName),
           bootEntry.isDir,
           bootEntry.firstCluster >= 2,
           let bootEntries = readDirEntries(block, vol, fat, startCluster: bootEntry.firstCluster, intent: IO_INTENT_THROUGHPUT),
           let found = findKernelName(in: bootEntries, candidates: candidates) {
            var out: [UInt8] = []
            out.append(contentsOf: bootName)
            out.append(0x2F)
            out.append(contentsOf: found)
            return out
        }
    }
    return nil
}

struct FileCapEntry {
    let id: UInt32
    let rights: UInt32
    let path: [UInt8]
}

private var gFileCaps: [FileCapEntry] = []
private var gNextFileCapId: UInt32 = 1

func issueFileCap(path: [UInt8], rights: UInt32) -> UInt32 {
    if gFileCaps.count >= 64 { return 0 }
    let id = gNextFileCapId
    gNextFileCapId &+= 1
    gFileCaps.append(FileCapEntry(id: id, rights: rights, path: path))
    return id
}

func issueFat32Contract(rights: UInt32, policy: UInt32) -> UInt64 {
    var handle: UInt64 = 0
    let st = cka_contract_open(KS_CONTRACT_FAT32, rights, policy, &handle)
    if st != KS_STATUS_OK || handle == 0 {
        return 0
    }
    return handle
}

private func lookupFileCap(_ id: UInt32) -> FileCapEntry? {
    for entry in gFileCaps {
        if entry.id == id { return entry }
    }
    return nil
}

private func lookupFat32Contract(_ handle: UInt64) -> (rights: UInt32, policy: UInt32)? {
    var rights: UInt32 = 0
    var policy: UInt32 = 0
    let st = cka_contract_get(handle, &rights, &policy)
    if st != KS_STATUS_OK {
        return nil
    }
    return (rights, policy)
}

struct TaskContext {
    var capIds: [UInt32] = []
    var contractIds: [UInt64] = []
    
    mutating func addCap(_ id: UInt32) {
        capIds.append(id)
    }

    mutating func addContract(_ id: UInt64) {
        contractIds.append(id)
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

    func hasContract(rights: UInt32) -> Bool {
        for id in contractIds {
            if let entry = lookupFat32Contract(id) {
                if (entry.rights & rights) == rights {
                    return true
                }
            }
        }
        return false
    }

    func contractPolicy(rights: UInt32) -> UInt32? {
        for id in contractIds {
            if let entry = lookupFat32Contract(id) {
                if (entry.rights & rights) == rights {
                    return entry.policy
                }
            }
        }
        return nil
    }
    
    func canListRoot() -> Bool {
        return hasRights(path: [0x2F], rights: CAP_R_DIR_LIST)
    }
    
    func canReadFile(path: [UInt8]) -> Bool {
        return hasRights(path: path, rights: CAP_R_FILE_READ)
    }
    
    func canListDir(path: [UInt8]) -> Bool {
        return hasRights(path: path, rights: CAP_R_DIR_LIST)
    }

    func canWriteFileSafe(path: [UInt8]) -> Bool {
        return hasRights(path: path, rights: CAP_R_FILE_WRITE_SAFE)
    }

    func canWriteFileFast(path: [UInt8]) -> Bool {
        return hasRights(path: path, rights: CAP_R_FILE_WRITE_FAST)
    }
}

func fat32ReadFileData(task: inout TaskContext, path: NormalizedPath, intent: UInt32) -> [UInt8]? {
    if !task.canReadFile(path: path.flat) || !task.hasContract(rights: FAT32_CONTRACT_R_READ) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        fat32StatsRecordError()
        return nil
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        fat32StatsRecordError()
        return nil
    }
    guard let vol = mountFat32(block) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT32 volume\r\n")))
        fat32StatsRecordError()
        return nil
    }
    guard let fat = readFat32FAT(block, vol) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
        fat32StatsRecordError()
        return nil
    }
    guard let (entry, _) = resolvePathEntryLocation(block, vol, fat, path) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        fat32StatsRecordError()
        return nil
    }
    if entry.isDir {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a file\r\n")))
        fat32StatsRecordError()
        return nil
    }
    if entry.fileSize == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty file\r\n")))
        return []
    }
    if entry.firstCluster < 2 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] invalid file cluster\r\n")))
        fat32StatsRecordError()
        return nil
    }
    fat32StatsRecordFileRead()
    let clusterSizeBytes = UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster)
    var remaining = entry.fileSize
    var cluster = entry.firstCluster
    var output: [UInt8] = []
    var guardCount: UInt32 = 0
    while cluster >= 2 && remaining > 0 && guardCount < 0x100000 {
        guard let data = readCluster(block, vol, cluster, intent: intent) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read file data\r\n")))
            fat32StatsRecordError()
            return nil
        }
        output.append(contentsOf: data)
        let next = fat32NextCluster(fat, cluster)
        if next >= 0x0FFFFFF8 || next == 0x0FFFFFF7 {
            break
        }
        cluster = next
        if remaining > clusterSizeBytes {
            remaining -= clusterSizeBytes
        } else {
            remaining = 0
        }
        guardCount &+= 1
    }
    if output.count > Int(entry.fileSize) {
        output = Array(output[0..<Int(entry.fileSize)])
    }
    return output
}

func fat32WriteFileData(task: inout TaskContext, path: NormalizedPath, data: [UInt8], safe: Bool, emitOk: Bool) -> Bool {
    let _ = safe
    let policy = task.contractPolicy(rights: FAT32_CONTRACT_R_WRITE)
    if policy == nil {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        fat32StatsRecordError()
        return false
    }
    if policy == FAT32_POLICY_BACKGROUND {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        fat32StatsRecordError()
        return false
    }
    let needSafe = (policy == FAT32_POLICY_SAFE)
    let allow = needSafe ? task.canWriteFileSafe(path: path.flat) : task.canWriteFileFast(path: path.flat)
    if !allow {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        fat32StatsRecordError()
        return false
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        fat32StatsRecordError()
        return false
    }
    guard let vol = mountFat32(block) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT32 volume\r\n")))
        fat32StatsRecordError()
        return false
    }
    guard let fat = readFat32FAT(block, vol) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
        fat32StatsRecordError()
        return false
    }
    guard let (entry, location) = resolvePathEntryLocation(block, vol, fat, path) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        fat32StatsRecordError()
        return false
    }
    if entry.isDir {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a file\r\n")))
        fat32StatsRecordError()
        return false
    }
    let clusterSizeBytes = Int(UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster))
    if clusterSizeBytes == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] invalid cluster size\r\n")))
        fat32StatsRecordError()
        return false
    }
    let targetSize = data.count
    if targetSize == 0 {
        if !updateDirEntryOnDisk(block, vol, location, newFirstCluster: entry.firstCluster, newSize: 0, safe: needSafe) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to update dir entry\r\n")))
            fat32StatsRecordError()
            return false
        }
        fat32StatsRecordFileWrite()
        if emitOk {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] write ok\r\n")))
        }
        return true
    }
    if data.isEmpty {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] nothing to write\r\n")))
        return false
    }
    let requiredClusters = (targetSize + clusterSizeBytes - 1) / clusterSizeBytes
    var chain: [UInt32] = []
    if entry.firstCluster >= 2 {
        chain = fat32ClusterChain(vol, fat, entry.firstCluster)
    }
    let oldCount = chain.count
    if requiredClusters > chain.count {
        let need = requiredClusters - chain.count
        guard let newClusters = fat32FindFreeClusters(vol, fat, count: need) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no free clusters\r\n")))
            fat32StatsRecordError()
            return false
        }
        if chain.isEmpty {
            chain = newClusters
        } else {
            chain.append(contentsOf: newClusters)
        }
        var fatCopy = fat
        if oldCount > 0 {
            fat32SetCluster(&fatCopy, chain[oldCount - 1], newClusters[0])
        }
        for i in 0..<newClusters.count {
            let val: UInt32 = (i + 1 < newClusters.count) ? newClusters[i + 1] : 0x0FFFFFFF
            fat32SetCluster(&fatCopy, newClusters[i], val)
        }
        if !writeFatTables(block, vol, fatCopy, safe: needSafe) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to write FAT\r\n")))
            fat32StatsRecordError()
            return false
        }
        if !chain.isEmpty {
            if !updateDirEntryOnDisk(block, vol, location, newFirstCluster: chain[0], newSize: UInt32(targetSize), safe: needSafe) {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to update dir entry\r\n")))
                fat32StatsRecordError()
                return false
            }
        }
        if emitOk {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] fat extended\r\n")))
        }
    }
    if chain.isEmpty {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no cluster chain\r\n")))
        fat32StatsRecordError()
        return false
    }
    var remaining = targetSize
    var dataOffset = 0
    var guardCount: UInt32 = 0
    for i in 0..<requiredClusters {
        if guardCount >= 0x100000 { break }
        let cluster = chain[i]
        var clusterData: [UInt8]
        if i < oldCount {
            guard let existing = readCluster(block, vol, cluster, intent: IO_INTENT_THROUGHPUT) else {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read file data\r\n")))
                fat32StatsRecordError()
                return false
            }
            if existing.count < clusterSizeBytes {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] short cluster read\r\n")))
                fat32StatsRecordError()
                return false
            }
            clusterData = existing
        } else {
            clusterData = [UInt8](repeating: 0, count: clusterSizeBytes)
        }
        let writeCount = min(remaining, clusterSizeBytes)
        clusterData.replaceSubrange(0..<writeCount, with: data[dataOffset..<(dataOffset + writeCount)])
        if !writeCluster(block, vol, cluster, data: clusterData, safe: needSafe) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to write file data\r\n")))
            fat32StatsRecordError()
            return false
        }
        dataOffset += writeCount
        remaining -= writeCount
        if remaining == 0 { break }
        guardCount &+= 1
    }
    if !updateDirEntryOnDisk(block, vol, location, newFirstCluster: chain[0], newSize: UInt32(targetSize), safe: needSafe) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to update dir entry\r\n")))
        fat32StatsRecordError()
        return false
    }
    fat32StatsRecordFileWrite()
    if emitOk {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] write ok\r\n")))
    }
    return true
}

func readFat32File(task: inout TaskContext, path: NormalizedPath) {
    let readIntent: UInt32
    if task.contractPolicy(rights: FAT32_CONTRACT_R_READ) == FAT32_POLICY_BACKGROUND {
        readIntent = IO_INTENT_BACKGROUND
    } else {
        readIntent = IO_INTENT_LATENCY
    }
    guard let output = fat32ReadFileData(task: &task, path: path, intent: readIntent) else {
        return
    }
    gConsole?.writeUserOutput(output)
    gConsole?.writeUserOutput([0x0D, 0x0A])
}

func writeFat32File(task: inout TaskContext, path: NormalizedPath, data: [UInt8], safe: Bool) {
    _ = fat32WriteFileData(task: &task, path: path, data: data, safe: safe, emitOk: true)
}

func fat32SmokeTest(path: NormalizedPath, expectedSize: UInt32, expectedChecksum: UInt32) -> Bool {
    var task = TaskContext()
    let cap = issueFileCap(path: path.flat, rights: CAP_R_FILE_READ)
    if cap == 0 {
        return false
    }
    task.addCap(cap)
    let intent: UInt32 = IO_INTENT_THROUGHPUT
    guard let data = fat32ReadFileData(task: &task, path: path, intent: intent) else {
        return false
    }
    if UInt32(data.count) != expectedSize {
        return false
    }
    let sum = fatChecksum32(data)
    return sum == expectedChecksum
}

func fat32DeleteFile(path: NormalizedPath, safe: Bool) -> Bool {
    if path.isRoot { return false }
    guard let block = gBlockDevice else { return false }
    guard let vol = mountFat32(block) else { return false }
    guard var fat = readFat32FAT(block, vol) else { return false }
    guard let (entry, location) = resolvePathEntryLocation(block, vol, fat, path) else { return false }
    if entry.isDir { return false }
    if entry.firstCluster >= 2 {
        let chain = fat32ClusterChain(vol, fat, entry.firstCluster)
        for c in chain {
            fat32SetCluster(&fat, c, 0)
        }
        if !writeFatTables(block, vol, fat, safe: safe) {
            return false
        }
    }
    if !markDirEntryDeleted(block, vol, location, safe: safe) {
        return false
    }
    return true
}

func runTestTask(path: NormalizedPath) {
    var test = TaskContext()
    let fileCap = issueFileCap(path: path.flat, rights: CAP_R_FILE_READ)
    if fileCap == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] testtask: cap table full\r\n")))
        return
    }
    test.addCap(fileCap)
    let contract = issueFat32Contract(rights: FAT32_CONTRACT_R_READ, policy: FAT32_POLICY_FAST)
    if contract != 0 {
        test.addContract(contract)
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] testtask: list root -> ")))
    if test.canListRoot() {
        gConsole?.writeUserOutput(Array(staticStringBytes("allowed\r\n")))
    } else {
        gConsole?.writeUserOutput(Array(staticStringBytes("denied\r\n")))
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] testtask: read file\r\n")))
    readFat32File(task: &test, path: path)
}

func runFsStressTest(path: NormalizedPath, iterations: Int, size: Int, policy: UInt32) {
    if iterations <= 0 || size <= 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: invalid parameters\r\n")))
        return
    }
    var task = TaskContext()
    let readCap = issueFileCap(path: path.flat, rights: CAP_R_FILE_READ)
    let writeRights: UInt32 = (policy == FAT32_POLICY_SAFE) ? CAP_R_FILE_WRITE_SAFE : CAP_R_FILE_WRITE_FAST
    let writeCap = issueFileCap(path: path.flat, rights: writeRights)
    if readCap == 0 || writeCap == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: cap table full\r\n")))
        return
    }
    task.addCap(readCap)
    task.addCap(writeCap)
    let contract = issueFat32Contract(rights: FAT32_CONTRACT_R_READ | FAT32_CONTRACT_R_WRITE,
                                      policy: policy)
    if contract == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: contract table full\r\n")))
        return
    }
    task.addContract(contract)
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: start\r\n")))
    for i in 0..<iterations {
        var data = [UInt8](repeating: 0, count: size)
        for j in 0..<size {
            data[j] = UInt8(truncatingIfNeeded: j + i)
        }
        if !fat32WriteFileData(task: &task, path: path, data: data, safe: policy == FAT32_POLICY_SAFE, emitOk: false) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: write failed\r\n")))
            return
        }
        guard let readBack = fat32ReadFileData(task: &task, path: path, intent: IO_INTENT_LATENCY) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: read failed\r\n")))
            return
        }
        if readBack.count < size {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: short read\r\n")))
            return
        }
        for j in 0..<size {
            if readBack[j] != data[j] {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: data mismatch\r\n")))
                return
            }
        }
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: iter ")))
        gConsole?.writeUserOutput(u64ToDecBytes(UInt64(i + 1)))
        gConsole?.writeUserOutput([0x0D, 0x0A])
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] stress: ok\r\n")))
}

func runIoReadTest(pathA: NormalizedPath, pathB: NormalizedPath) {
    var taskA = TaskContext()
    var taskB = TaskContext()
    let capA = issueFileCap(path: pathA.flat, rights: CAP_R_FILE_READ)
    let capB = issueFileCap(path: pathB.flat, rights: CAP_R_FILE_READ)
    if capA == 0 || capB == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] ioreadtest: cap table full\r\n")))
        return
    }
    taskA.addCap(capA)
    taskB.addCap(capB)
    let contractA = issueFat32Contract(rights: FAT32_CONTRACT_R_READ, policy: FAT32_POLICY_FAST)
    let contractB = issueFat32Contract(rights: FAT32_CONTRACT_R_READ, policy: FAT32_POLICY_FAST)
    if contractA != 0 {
        taskA.addContract(contractA)
    }
    if contractB != 0 {
        taskB.addContract(contractB)
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[iotest] A=LATENCY B=BACKGROUND\r\n")))
    
    guard var stateA = prepareFat32ReadState(task: taskA, path: pathA, intent: IO_INTENT_LATENCY, label: 0x41),
          var stateB = prepareFat32ReadState(task: taskB, path: pathB, intent: IO_INTENT_BACKGROUND, label: 0x42) else {
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

private struct Fat32ReadState {
    let vol: Fat32Volume
    let fat: [UInt8]
    var cluster: UInt32
    var remaining: UInt32
    let intent: UInt32
    let label: UInt8
}

private func prepareFat32ReadState(task: TaskContext, path: NormalizedPath, intent: UInt32, label: UInt8) -> Fat32ReadState? {
    if !task.canReadFile(path: path.flat) || !task.hasContract(rights: FAT32_CONTRACT_R_READ) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return nil
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return nil
    }
    guard let vol = mountFat32(block) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT32 volume\r\n")))
        return nil
    }
    guard let fat = readFat32FAT(block, vol) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
        return nil
    }
    guard let entry = resolvePathEntry(block, vol, fat, path) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        return nil
    }
    if entry.isDir || entry.fileSize == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty file\r\n")))
        return nil
    }
    if entry.firstCluster < 2 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] invalid file cluster\r\n")))
        return nil
    }
    return Fat32ReadState(vol: vol, fat: fat, cluster: entry.firstCluster, remaining: entry.fileSize, intent: intent, label: label)
}

private func readOneCluster(_ state: inout Fat32ReadState) -> Bool {
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
    let next = fat32NextCluster(state.fat, state.cluster)
    if next >= 0x0FFFFFF8 || next == 0x0FFFFFF7 {
        state.remaining = 0
    } else {
        state.cluster = next
    }
    gConsole?.writeUserOutput([state.label, 0x2E])
    return true
}

private func readFat32FileWithIntent(task: inout TaskContext, path: NormalizedPath, intent: UInt32, label: StaticString) {
    if !task.canReadFile(path: path.flat) || !task.hasContract(rights: FAT32_CONTRACT_R_READ) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return
    }
    guard let vol = mountFat32(block) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT32 volume\r\n")))
        return
    }
    guard let fat = readFat32FAT(block, vol) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
        return
    }
    guard let entry = resolvePathEntry(block, vol, fat, path) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        return
    }
    if entry.isDir || entry.fileSize == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty file\r\n")))
        return
    }
    if entry.firstCluster < 2 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] invalid file cluster\r\n")))
        return
    }
    gConsole?.writeUserOutput(Array(staticStringBytes(label)))
    gConsole?.writeUserOutput(Array(staticStringBytes(": start\r\n")))
    let clusterSizeBytes = UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster)
    var remaining = entry.fileSize
    var cluster = entry.firstCluster
    var totalRead: UInt32 = 0
    var guardCount: UInt32 = 0
    while cluster >= 2 && remaining > 0 && guardCount < 0x100000 {
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
        let next = fat32NextCluster(fat, cluster)
        if next >= 0x0FFFFFF8 || next == 0x0FFFFFF7 {
            break
        }
        cluster = next
        if remaining > clusterSizeBytes {
            remaining -= clusterSizeBytes
        } else {
            remaining = 0
        }
        guardCount &+= 1
    }
    gConsole?.writeUserOutput([0x0D, 0x0A])
    gConsole?.writeUserOutput(Array(staticStringBytes(label)))
    gConsole?.writeUserOutput(Array(staticStringBytes(": done\r\n")))
}

func runKernelReadTest(path: NormalizedPath) {
    var task = TaskContext()
    let cap = issueFileCap(path: path.flat, rights: CAP_R_FILE_READ)
    if cap == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kerneltest: cap table full\r\n")))
        return
    }
    task.addCap(cap)
    let contract = issueFat32Contract(rights: FAT32_CONTRACT_R_READ, policy: FAT32_POLICY_FAST)
    if contract == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kerneltest: contract table full\r\n")))
        return
    }
    task.addContract(contract)
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kerneltest: read contract ok\r\n")))
    guard var state = prepareFat32ReadState(task: task, path: path, intent: IO_INTENT_LATENCY, label: 0x4B) else {
        return
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] kerneltest: size ")))
    gConsole?.writeUserOutput(u64ToDecBytes(UInt64(state.remaining)))
    gConsole?.writeUserOutput(Array(staticStringBytes(" bytes\r\n")))
    _ = readOneCluster(&state)
    gConsole?.writeUserOutput(Array(staticStringBytes("\r\n[fs] kerneltest: first cluster ok\r\n")))
}
