//
//  FatCommon.swift
//  Core
//

// Shared helpers for FAT16/FAT32 parsing.

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

enum FatType {
    case fat16
    case fat32
}

struct FatBPB {
    let fatType: FatType
    let bytesPerSector: UInt16
    let sectorsPerCluster: UInt8
    let reservedSectors: UInt16
    let numFATs: UInt8
    let rootEntryCount: UInt16
    let totalSectors: UInt32
    let sectorsPerFAT: UInt32
    let rootCluster: UInt32
    let baseLBA: UInt32
    let fatStartLBA: UInt32
    let dataStartLBA: UInt32
    let rootDirStartLBA: UInt32
    let rootDirSectors: UInt32
}

private func fatRootDirSectors(_ bytesPerSector: UInt16, _ rootEntryCount: UInt16) -> UInt32 {
    if bytesPerSector == 0 || rootEntryCount == 0 { return 0 }
    let bytes = UInt32(rootEntryCount) * 32
    return (bytes + UInt32(bytesPerSector) - 1) / UInt32(bytesPerSector)
}

private func fatDetectType(totalSectors: UInt32,
                           reservedSectors: UInt16,
                           numFATs: UInt8,
                           sectorsPerFAT: UInt32,
                           rootDirSectors: UInt32,
                           sectorsPerCluster: UInt8) -> FatType? {
    if sectorsPerCluster == 0 {
        return nil
    }
    let dataSectors = Int64(totalSectors)
        - Int64(reservedSectors)
        - Int64(numFATs) * Int64(sectorsPerFAT)
        - Int64(rootDirSectors)
    if dataSectors <= 0 { return nil }
    let clusters = UInt64(dataSectors) / UInt64(sectorsPerCluster)
    if clusters < 4085 {
        return nil // FAT12 unsupported
    }
    if clusters < 65525 {
        return .fat16
    }
    return .fat32
}

func fatParseBPB(_ data: [UInt8], baseLBA: UInt32) -> FatBPB? {
    if data.count < 90 { return nil }
    let bytesPerSector = readU16LE(data, 11)
    let sectorsPerCluster = data[13]
    let reservedSectors = readU16LE(data, 14)
    let numFATs = data[16]
    let rootEntryCount = readU16LE(data, 17)
    let totalSectors16 = readU16LE(data, 19)
    let sectorsPerFAT16 = readU16LE(data, 22)
    let totalSectors32 = readU32LE(data, 32)
    let sectorsPerFAT32 = readU32LE(data, 36)
    let rootCluster = readU32LE(data, 44)

    let totalSectors = totalSectors16 != 0 ? UInt32(totalSectors16) : totalSectors32
    let sectorsPerFAT = sectorsPerFAT16 != 0 ? UInt32(sectorsPerFAT16) : sectorsPerFAT32
    if bytesPerSector == 0 || sectorsPerCluster == 0 || sectorsPerFAT == 0 || totalSectors == 0 {
        return nil
    }

    let rootDirSectors = fatRootDirSectors(bytesPerSector, rootEntryCount)
    guard let fatType = fatDetectType(totalSectors: totalSectors,
                                      reservedSectors: reservedSectors,
                                      numFATs: numFATs,
                                      sectorsPerFAT: sectorsPerFAT,
                                      rootDirSectors: rootDirSectors,
                                      sectorsPerCluster: sectorsPerCluster) else {
        return nil
    }

    if fatType == .fat32 {
        if rootEntryCount != 0 || sectorsPerFAT16 != 0 {
            return nil
        }
        if rootCluster < 2 {
            return nil
        }
    }

    let fatStartLBA = baseLBA + UInt32(reservedSectors)
    let dataStartLBA = fatStartLBA + UInt32(numFATs) * sectorsPerFAT + rootDirSectors
    let rootDirStartLBA = fatStartLBA + UInt32(numFATs) * sectorsPerFAT

    return FatBPB(fatType: fatType,
                  bytesPerSector: bytesPerSector,
                  sectorsPerCluster: sectorsPerCluster,
                  reservedSectors: reservedSectors,
                  numFATs: numFATs,
                  rootEntryCount: rootEntryCount,
                  totalSectors: totalSectors,
                  sectorsPerFAT: sectorsPerFAT,
                  rootCluster: rootCluster,
                  baseLBA: baseLBA,
                  fatStartLBA: fatStartLBA,
                  dataStartLBA: dataStartLBA,
                  rootDirStartLBA: rootDirStartLBA,
                  rootDirSectors: rootDirSectors)
}

struct FatDirEntry {
    let name: [UInt8]
    let attr: UInt8
    let firstCluster: UInt32
    let fileSize: UInt32

    var isDir: Bool { (attr & 0x10) != 0 }
}

struct FatDirEntryRecord {
    let entry: FatDirEntry
    let offset: Int
}

private func fatDecodeShortName(_ data: [UInt8], _ off: Int) -> [UInt8]? {
    if off + 32 > data.count { return nil }
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

private func fatAppendLfnChars(_ data: [UInt8], _ off: Int, _ out: inout [UInt16]) {
    // LFN name fields: 1-10, 14-25, 28-31 (UTF-16LE)
    let ranges = [(1, 10), (14, 25), (28, 31)]
    for (start, end) in ranges {
        var i = start
        while i <= end {
            if off + i + 1 >= data.count { return }
            let lo = UInt16(data[off + i])
            let hi = UInt16(data[off + i + 1]) << 8
            let ch = lo | hi
            if ch == 0x0000 || ch == 0xFFFF {
                return
            }
            out.append(ch)
            i += 2
        }
    }
}

private func fatDecodeLfn(_ segments: [[UInt16]]) -> [UInt8]? {
    if segments.isEmpty { return nil }
    var out: [UInt8] = []
    var totalCount = 0
    for seg in segments { totalCount += seg.count }
    out.reserveCapacity(totalCount)
    for seg in segments.reversed() {
        for ch in seg {
            if ch <= 0x7F {
                out.append(UInt8(truncatingIfNeeded: ch))
            } else {
                out.append(UInt8(ascii: "?"))
            }
        }
    }
    return out.isEmpty ? nil : out
}

func fatScanDirEntries(_ data: [UInt8], fatType: FatType, includeLFN: Bool) -> [FatDirEntryRecord] {
    return fatScanDirEntriesEx(data, fatType: fatType, includeLFN: includeLFN).records
}

func fatScanDirEntriesEx(_ data: [UInt8], fatType: FatType, includeLFN: Bool) -> (records: [FatDirEntryRecord], endReached: Bool) {
    var records: [FatDirEntryRecord] = []
    var lfnSegments: [[UInt16]] = []
    var off = 0
    var endReached = false
    while off + 32 <= data.count {
        let first = data[off]
        if first == 0x00 {
            endReached = true
            break
        }
        if first == 0xE5 {
            lfnSegments.removeAll(keepingCapacity: true)
            off += 32
            continue
        }
        let attr = data[off + 11]
        if attr == 0x0F {
            if includeLFN {
                var seg: [UInt16] = []
                fatAppendLfnChars(data, off, &seg)
                if !seg.isEmpty {
                    lfnSegments.append(seg)
                }
            }
            off += 32
            continue
        }
        if (attr & 0x08) != 0 {
            lfnSegments.removeAll(keepingCapacity: true)
            off += 32
            continue
        }

        let name: [UInt8]?
        if includeLFN, let lfn = fatDecodeLfn(lfnSegments) {
            name = lfn
        } else {
            name = fatDecodeShortName(data, off)
        }
        lfnSegments.removeAll(keepingCapacity: true)
        guard let entryName = name else {
            off += 32
            continue
        }

        let hi = UInt32(readU16LE(data, off + 20))
        let lo = UInt32(readU16LE(data, off + 26))
        let firstCluster = (hi << 16) | lo
        let fileSize = readU32LE(data, off + 28)
        let entry = FatDirEntry(name: entryName, attr: attr, firstCluster: firstCluster, fileSize: fileSize)
        records.append(FatDirEntryRecord(entry: entry, offset: off))
        off += 32
    }
    return (records, endReached)
}

private func fatEocThreshold(_ fatType: FatType) -> UInt32 {
    return fatType == .fat32 ? 0x0FFFFFF8 : 0xFFF8
}

private func fatBadCluster(_ fatType: FatType) -> UInt32 {
    return fatType == .fat32 ? 0x0FFFFFF7 : 0xFFF7
}

func fatNextCluster(_ fat: [UInt8], fatType: FatType, cluster: UInt32) -> UInt32 {
    switch fatType {
    case .fat32:
        let off = Int(cluster * 4)
        if off + 3 >= fat.count { return 0x0FFFFFFF }
        return readU32LE(fat, off) & 0x0FFFFFFF
    case .fat16:
        let off = Int(cluster * 2)
        if off + 1 >= fat.count { return 0xFFFF }
        return UInt32(readU16LE(fat, off))
    }
}

func fatClusterChain(fatType: FatType,
                     fat: [UInt8],
                     start: UInt32,
                     maxCluster: UInt32) -> [UInt32]? {
    if start < 2 || maxCluster < 2 {
        return nil
    }
    var chain: [UInt32] = []
    var cluster = start
    var guardCount: UInt32 = 0
    let eoc = fatEocThreshold(fatType)
    let bad = fatBadCluster(fatType)
    while cluster >= 2 && cluster <= maxCluster && guardCount < 0x100000 {
        chain.append(cluster)
        let next = fatNextCluster(fat, fatType: fatType, cluster: cluster)
        if next == bad {
            return nil
        }
        if next >= eoc {
            break
        }
        if next < 2 {
            return nil
        }
        cluster = next
        guardCount &+= 1
    }
    return chain
}

func fatFindPartitionLBAFromMBR(_ mbr: [UInt8], types: [UInt8]) -> UInt32? {
    if mbr.count < 512 { return nil }
    if mbr[510] != 0x55 || mbr[511] != 0xAA {
        return nil
    }
    let partTable = 446
    for i in 0..<4 {
        let off = partTable + i * 16
        if off + 15 >= mbr.count { break }
        let type = mbr[off + 4]
        if types.contains(type) {
            let lba = readU32LE(mbr, off + 8)
            if lba != 0 {
                return lba
            }
        }
    }
    return nil
}

func fatFindPartitionLBAFromGPT(_ block: BlockDevice) -> UInt64? {
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
        if typeGuid == efiSystemGuid || typeGuid == msBasicDataGuid {
            let firstLBA = readU64LE(entriesData, off + 32)
            if firstLBA != 0 {
                return firstLBA
            }
        }
    }
    return nil
}

func fatChecksum32(_ data: [UInt8]) -> UInt32 {
    var sum: UInt32 = 0
    for b in data {
        sum = (sum << 5) &- sum &+ UInt32(b)
    }
    return sum
}

func findKernelName(in entries: [FatDirEntry], candidates: [[UInt8]]) -> [UInt8]? {
    for cand in candidates {
        for entry in entries {
            if !entry.isDir && entry.name == cand {
                return cand
            }
        }
    }
    return nil
}

func fatEncodeShortName(_ name: [UInt8]) -> [UInt8]? {
    if name.isEmpty { return nil }
    var base: [UInt8] = []
    var ext: [UInt8] = []
    var seenDot = false
    for b in name {
        if b == 0x2E {
            if seenDot { return nil }
            seenDot = true
            continue
        }
        if !seenDot {
            base.append(toUpperASCII(b))
        } else {
            ext.append(toUpperASCII(b))
        }
    }
    if base.isEmpty || base.count > 8 || ext.count > 3 {
        return nil
    }
    var out = [UInt8](repeating: 0x20, count: 11)
    for i in 0..<base.count { out[i] = base[i] }
    for i in 0..<ext.count { out[8 + i] = ext[i] }
    return out
}
