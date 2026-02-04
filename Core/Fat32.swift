//
//  Fat32.swift
//  Core
//

func writeU32LE(_ value: UInt32, _ out: inout [UInt8], _ offset: Int) {
    out[offset + 0] = UInt8(truncatingIfNeeded: value)
    out[offset + 1] = UInt8(truncatingIfNeeded: value >> 8)
    out[offset + 2] = UInt8(truncatingIfNeeded: value >> 16)
    out[offset + 3] = UInt8(truncatingIfNeeded: value >> 24)
}

func writeU16LE(_ value: UInt16, _ out: inout [UInt8], _ offset: Int) {
    out[offset + 0] = UInt8(truncatingIfNeeded: value)
    out[offset + 1] = UInt8(truncatingIfNeeded: value >> 8)
}

func readU32LE(_ data: [UInt8], _ offset: Int) -> UInt32 {
    if data.count < offset + 4 {
        return 0
    }
    return UInt32(data[offset + 0])
    | (UInt32(data[offset + 1]) << 8)
    | (UInt32(data[offset + 2]) << 16)
    | (UInt32(data[offset + 3]) << 24)
}

func readU16LE(_ data: [UInt8], _ offset: Int) -> UInt16 {
    if data.count < offset + 2 {
        return 0
    }
    return UInt16(data[offset + 0]) | (UInt16(data[offset + 1]) << 8)
}

func readU64LE(_ data: [UInt8], _ offset: Int) -> UInt64 {
    if data.count < offset + 8 {
        return 0
    }
    var value: UInt64 = 0
    for i in 0..<8 {
        value |= UInt64(data[offset + i]) << (UInt64(i) * 8)
    }
    return value
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

    func write(lba: UInt64, count: UInt32, data: [UInt8], intent: UInt32) -> Bool {
        if count == 0 {
            return true
        }
        let byteCount = Int(count) * Int(sectorSize)
        if data.count < byteCount {
            return false
        }
        let st = data.withUnsafeBytes { raw -> Int32 in
            guard let base = raw.baseAddress else { return KS_STATUS_INVALID_ARG }
            return cka_block_write_intent(lba, count, base, byteCount, intent)
        }
        return st == KS_STATUS_OK
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
    if data.count < 90 { return nil }
    let bytesPerSector = readU16LE(data, 11)
    let sectorsPerCluster = data[13]
    let reservedSectors = readU16LE(data, 14)
    let numFATs = data[16]
    let rootEntryCount = readU16LE(data, 17)
    let totalSectors16 = readU16LE(data, 19)
    let sectorsPerFAT16 = readU16LE(data, 22)
    let totalSectors32 = readU32LE(data, 32)
    let sectorsPerFAT = readU32LE(data, 36)
    let rootCluster = readU32LE(data, 44)
    let totalSectors = totalSectors16 != 0 ? UInt32(totalSectors16) : totalSectors32
    if bytesPerSector == 0 || sectorsPerCluster == 0 || sectorsPerFAT == 0 || totalSectors == 0 {
        return nil
    }
    // FAT32 sanity checks.
    if rootEntryCount != 0 || sectorsPerFAT16 != 0 {
        return nil
    }
    if rootCluster < 2 {
        return nil
    }
    let fatStartLBA = baseLBA + UInt32(reservedSectors)
    let dataStartLBA = fatStartLBA + UInt32(numFATs) * sectorsPerFAT
    return Fat32Volume(bytesPerSector: bytesPerSector,
                       sectorsPerCluster: sectorsPerCluster,
                       reservedSectors: reservedSectors,
                       numFATs: numFATs,
                       sectorsPerFAT: sectorsPerFAT,
                       totalSectors: totalSectors,
                       rootCluster: rootCluster,
                       baseLBA: baseLBA,
                       fatStartLBA: fatStartLBA,
                       dataStartLBA: dataStartLBA,
                       walStartLBA: 0,
                       walSectors: 0)
}

private func findFat32PartitionLBA(_ mbr: [UInt8]) -> UInt32? {
    if mbr.count < 512 { return nil }
    if mbr[510] != 0x55 || mbr[511] != 0xAA {
        return nil
    }
    let partTable = 446
    for i in 0..<4 {
        let off = partTable + i * 16
        if off + 15 >= mbr.count { break }
        let type = mbr[off + 4]
        if type == 0x0B || type == 0x0C {
            let lba = readU32LE(mbr, off + 8)
            if lba != 0 {
                return lba
            }
        }
    }
    return nil
}

private func gptGuidMatches(_ entry: [UInt8], _ guid: [UInt8]) -> Bool {
    if entry.count < 16 || guid.count != 16 { return false }
    for i in 0..<16 {
        if entry[i] != guid[i] { return false }
    }
    return true
}

private func findFat32PartitionLBAFromGPT(_ block: BlockDevice) -> UInt64? {
    guard let header = block.read(lba: 1, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if header.count < 92 { return nil }
    if header[0] != 0x45 || header[1] != 0x46 || header[2] != 0x49 || header[3] != 0x20 ||
       header[4] != 0x50 || header[5] != 0x41 || header[6] != 0x52 || header[7] != 0x54 {
        return nil
    }
    let entriesLBA = readU64LE(header, 72)
    let numEntries = readU32LE(header, 80)
    let entrySize = readU32LE(header, 84)
    if entriesLBA == 0 || numEntries == 0 || entrySize < 128 {
        return nil
    }
    let sectorSize = UInt64(block.sectorSize)
    let totalBytes = UInt64(numEntries) * UInt64(entrySize)
    let sectors = (totalBytes + sectorSize - 1) / sectorSize
    if sectors == 0 {
        return nil
    }
    guard let entriesData = block.read(lba: entriesLBA, count: UInt32(sectors), intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    let efiSystemGuid: [UInt8] = [0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B]
    let msBasicDataGuid: [UInt8] = [0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44, 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7]
    let entryStride = Int(entrySize)
    let maxEntries = min(Int(numEntries), entriesData.count / entryStride)
    for i in 0..<maxEntries {
        let off = i * entryStride
        let typeGuid = Array(entriesData[off..<(off + 16)])
        if gptGuidMatches(typeGuid, efiSystemGuid) || gptGuidMatches(typeGuid, msBasicDataGuid) {
            let firstLBA = readU64LE(entriesData, off + 32)
            if firstLBA != 0 {
                return firstLBA
            }
        }
    }
    return nil
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
    return true
}

private func mountFat32(_ block: BlockDevice) -> Fat32Volume? {
    guard let boot0 = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if let vol = parseFat32BootSector(boot0, baseLBA: 0) {
        let out = configureWalRegion(vol)
        walReplayIfNeeded(block, out)
        return out
    }
    if let gptLBA = findFat32PartitionLBAFromGPT(block) {
        if gptLBA <= UInt64(UInt32.max),
           let boot = block.read(lba: gptLBA, count: 1, intent: IO_INTENT_THROUGHPUT) {
            if let vol = parseFat32BootSector(boot, baseLBA: UInt32(gptLBA)) {
                let out = configureWalRegion(vol)
                walReplayIfNeeded(block, out)
                return out
            }
        }
    }
    guard let partLBA = findFat32PartitionLBA(boot0) else {
        return nil
    }
    guard let boot = block.read(lba: UInt64(partLBA), count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if let vol = parseFat32BootSector(boot, baseLBA: partLBA) {
        let out = configureWalRegion(vol)
        walReplayIfNeeded(block, out)
        return out
    }
    return nil
}

private func trimSpaces(_ bytes: [UInt8]) -> [UInt8] {
    var start = 0
    var end = bytes.count
    while start < end && bytes[start] == 0x20 { start += 1 }
    while end > start && bytes[end - 1] == 0x20 { end -= 1 }
    if start >= end { return [] }
    return Array(bytes[start..<end])
}

private func readFat32FAT(_ block: BlockDevice, _ vol: Fat32Volume) -> [UInt8]? {
    if vol.sectorsPerFAT == 0 { return nil }
    var fatData: [UInt8] = []
    for i in 0..<vol.sectorsPerFAT {
        if let part = block.read(lba: UInt64(vol.fatStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            fatData.append(contentsOf: part)
        } else {
            return nil
        }
    }
    return fatData
}

private func fat32NextCluster(_ fat: [UInt8], _ cluster: UInt32) -> UInt32 {
    let off = Int(cluster * 4)
    if off + 3 >= fat.count { return 0x0FFFFFFF }
    let val = readU32LE(fat, off) & 0x0FFFFFFF
    return val
}

private func fat32SetCluster(_ fat: inout [UInt8], _ cluster: UInt32, _ value: UInt32) {
    let off = Int(cluster * 4)
    if off + 3 >= fat.count { return }
    let old = readU32LE(fat, off)
    let merged = (old & 0xF0000000) | (value & 0x0FFFFFFF)
    writeU32LE(merged, &fat, off)
}

private func fat32ClusterChain(_ fat: [UInt8], _ start: UInt32) -> [UInt32] {
    var chain: [UInt32] = []
    var cluster = start
    var guardCount: UInt32 = 0
    while cluster >= 2 && guardCount < 0x100000 {
        chain.append(cluster)
        let next = fat32NextCluster(fat, cluster)
        if next >= 0x0FFFFFF8 || next == 0x0FFFFFF7 {
            break
        }
        cluster = next
        guardCount &+= 1
    }
    return chain
}

private func fat32FindFreeClusters(_ fat: [UInt8], count: Int) -> [UInt32]? {
    if count <= 0 { return [] }
    var result: [UInt32] = []
    let maxClusters = fat.count / 4
    if maxClusters <= 2 { return nil }
    for c in 2..<maxClusters {
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
    let dataLBA = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
    var out: [UInt8] = []
    for s in 0..<UInt32(vol.sectorsPerCluster) {
        if let part = block.read(lba: dataLBA + UInt64(s), count: 1, intent: intent) {
            out.append(contentsOf: part)
        } else {
            return nil
        }
    }
    return out
}

private func writeCluster(_ block: BlockDevice, _ vol: Fat32Volume, _ cluster: UInt32, data: [UInt8], safe: Bool) -> Bool {
    let dataLBA = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
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

private struct Fat32DirEntry {
    let name: [UInt8]
    let attr: UInt8
    let firstCluster: UInt32
    let fileSize: UInt32
    
    var isDir: Bool { (attr & 0x10) != 0 }
}

private struct DirEntryLocation {
    let cluster: UInt32
    let entryOffset: Int
}

private func entryNameFromRaw(_ data: [UInt8], _ off: Int) -> [UInt8]? {
    if off + 32 > data.count { return nil }
    let first = data[off + 0]
    if first == 0x00 || first == 0xE5 { return nil }
    let attr = data[off + 11]
    if attr == 0x0F { return nil }
    if (attr & 0x08) != 0 { return nil }
    let nameBytes = trimSpaces(Array(data[off..<(off + 8)]))
    let extBytes = trimSpaces(Array(data[(off + 8)..<(off + 11)]))
    if nameBytes.isEmpty { return nil }
    var name: [UInt8] = []
    name.append(contentsOf: nameBytes.map { toUpperASCII($0) })
    if !extBytes.isEmpty {
        name.append(0x2E)
        name.append(contentsOf: extBytes.map { toUpperASCII($0) })
    }
    return name
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
        var off = 0
        while off + 32 <= clusterSize {
            let first = data[off + 0]
            if first == 0x00 {
                return nil
            }
            if let entryName = entryNameFromRaw(data, off), entryName == name {
                let attr = data[off + 11]
                let hi = UInt32(readU16LE(data, off + 20))
                let lo = UInt32(readU16LE(data, off + 26))
                let firstCluster = (hi << 16) | lo
                let fileSize = readU32LE(data, off + 28)
                let entry = Fat32DirEntry(name: entryName, attr: attr, firstCluster: firstCluster, fileSize: fileSize)
                return (entry, DirEntryLocation(cluster: cluster, entryOffset: off))
            }
            off += 32
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
        let entryCount = data.count / 32
        for i in 0..<entryCount {
            let off = i * 32
            let first = data[off]
            if first == 0x00 { return entries }
            if first == 0xE5 { continue }
            let attr = data[off + 11]
            if attr == 0x0F { continue }
            if (attr & 0x08) != 0 { continue }
            let nameBytes = trimSpaces(Array(data[off..<(off + 8)]))
            let extBytes = trimSpaces(Array(data[(off + 8)..<(off + 11)]))
            if nameBytes.isEmpty { continue }
            var name: [UInt8] = []
            name.append(contentsOf: nameBytes.map { toUpperASCII($0) })
            if !extBytes.isEmpty {
                name.append(0x2E)
                name.append(contentsOf: extBytes.map { toUpperASCII($0) })
            }
            let hi = UInt32(readU16LE(data, off + 20))
            let lo = UInt32(readU16LE(data, off + 26))
            let firstCluster = (hi << 16) | lo
            let fileSize = readU32LE(data, off + 28)
            entries.append(Fat32DirEntry(name: name, attr: attr, firstCluster: firstCluster, fileSize: fileSize))
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
    let entry: Fat32DirEntry
    if path.isRoot {
        entry = Fat32DirEntry(name: [0x2F], attr: 0x10, firstCluster: vol.rootCluster, fileSize: 0)
    } else {
        guard let found = resolvePathEntry(block, vol, fat, path) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] path not found\r\n")))
            return
        }
        entry = found
    }
    if !entry.isDir {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a directory\r\n")))
        return
    }
    let listIntent: UInt32
    if task.contractPolicy(rights: FAT32_CONTRACT_R_LIST) == FAT32_POLICY_BACKGROUND {
        listIntent = IO_INTENT_BACKGROUND
    } else {
        listIntent = IO_INTENT_THROUGHPUT
    }
    guard let entries = readDirEntries(block, vol, fat, startCluster: entry.firstCluster, intent: listIntent) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read directory\r\n")))
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

private func findKernelName(in entries: [Fat32DirEntry], candidates: [[UInt8]]) -> [UInt8]? {
    for cand in candidates {
        for entry in entries {
            if !entry.isDir && entry.name == cand {
                return cand
            }
        }
    }
    return nil
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

func readFat32File(task: inout TaskContext, path: NormalizedPath) {
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
    guard let (entry, _) = resolvePathEntryLocation(block, vol, fat, path) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        return
    }
    if entry.isDir {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a file\r\n")))
        return
    }
    if entry.fileSize == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] empty file\r\n")))
        return
    }
    if entry.firstCluster < 2 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] invalid file cluster\r\n")))
        return
    }
    let clusterSizeBytes = UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster)
    var remaining = entry.fileSize
    var cluster = entry.firstCluster
    var output: [UInt8] = []
    var guardCount: UInt32 = 0
    let readIntent: UInt32
    if task.contractPolicy(rights: FAT32_CONTRACT_R_READ) == FAT32_POLICY_BACKGROUND {
        readIntent = IO_INTENT_BACKGROUND
    } else {
        readIntent = IO_INTENT_LATENCY
    }
    while cluster >= 2 && remaining > 0 && guardCount < 0x100000 {
        guard let data = readCluster(block, vol, cluster, intent: readIntent) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read file data\r\n")))
            return
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
    gConsole?.writeUserOutput(output)
    gConsole?.writeUserOutput([0x0D, 0x0A])
}

func writeFat32File(task: inout TaskContext, path: NormalizedPath, data: [UInt8], safe: Bool) {
    let _ = safe
    let policy = task.contractPolicy(rights: FAT32_CONTRACT_R_WRITE)
    if policy == nil {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    if policy == FAT32_POLICY_BACKGROUND {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] denied\r\n")))
        return
    }
    let needSafe = (policy == FAT32_POLICY_SAFE)
    let allow = needSafe ? task.canWriteFileSafe(path: path.flat) : task.canWriteFileFast(path: path.flat)
    if !allow {
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
    guard let (entry, location) = resolvePathEntryLocation(block, vol, fat, path) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] file not found\r\n")))
        return
    }
    if entry.isDir {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a file\r\n")))
        return
    }
    let clusterSizeBytes = Int(UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster))
    if clusterSizeBytes == 0 {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] invalid cluster size\r\n")))
        return
    }
    let targetSize = data.count
    if targetSize == 0 {
        if !updateDirEntryOnDisk(block, vol, location, newFirstCluster: entry.firstCluster, newSize: 0, safe: needSafe) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to update dir entry\r\n")))
            return
        }
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] write ok\r\n")))
        return
    }
    if data.isEmpty {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] nothing to write\r\n")))
        return
    }
    let requiredClusters = (targetSize + clusterSizeBytes - 1) / clusterSizeBytes
    var chain: [UInt32] = []
    if entry.firstCluster >= 2 {
        chain = fat32ClusterChain(fat, entry.firstCluster)
    }
    let oldCount = chain.count
    if requiredClusters > chain.count {
        let need = requiredClusters - chain.count
        guard let newClusters = fat32FindFreeClusters(fat, count: need) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no free clusters\r\n")))
            return
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
            return
        }
        if !chain.isEmpty {
            if !updateDirEntryOnDisk(block, vol, location, newFirstCluster: chain[0], newSize: UInt32(targetSize), safe: needSafe) {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to update dir entry\r\n")))
                return
            }
        }
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] fat extended\r\n")))
    }
    if chain.isEmpty {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no cluster chain\r\n")))
        return
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
                return
            }
            if existing.count < clusterSizeBytes {
                gConsole?.writeUserOutput(Array(staticStringBytes("[fs] short cluster read\r\n")))
                return
            }
            clusterData = existing
        } else {
            clusterData = [UInt8](repeating: 0, count: clusterSizeBytes)
        }
        let writeCount = min(remaining, clusterSizeBytes)
        clusterData.replaceSubrange(0..<writeCount, with: data[dataOffset..<(dataOffset + writeCount)])
        if !writeCluster(block, vol, cluster, data: clusterData, safe: needSafe) {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to write file data\r\n")))
            return
        }
        dataOffset += writeCount
        remaining -= writeCount
        if remaining == 0 { break }
        guardCount &+= 1
    }
    if !updateDirEntryOnDisk(block, vol, location, newFirstCluster: chain[0], newSize: UInt32(targetSize), safe: needSafe) {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to update dir entry\r\n")))
        return
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("[fs] write ok\r\n")))
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
