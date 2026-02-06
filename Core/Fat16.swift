//
//  Fat16.swift
//  Core
//

private struct Fat16Volume {
    let bytesPerSector: UInt16
    let sectorsPerCluster: UInt8
    let reservedSectors: UInt16
    let numFATs: UInt8
    let sectorsPerFAT: UInt32
    let totalSectors: UInt32
    let baseLBA: UInt32
    let fatStartLBA: UInt32
    let dataStartLBA: UInt32
    let rootDirStartLBA: UInt32
    let rootDirSectors: UInt32
    let walStartLBA: UInt32
    let walSectors: UInt32
}

private var gFat16FatCache: [UInt8] = []
private var gFat16FatCacheSig: UInt64 = 0

private func fat16VolumeSig(_ vol: Fat16Volume) -> UInt64 {
    var sig: UInt64 = 0
    sig ^= UInt64(vol.baseLBA)
    sig = (sig << 16) ^ UInt64(vol.sectorsPerFAT)
    sig = (sig << 8) ^ UInt64(vol.numFATs)
    sig = (sig << 16) ^ UInt64(vol.dataStartLBA)
    sig = (sig << 16) ^ UInt64(vol.totalSectors)
    return sig
}

private func parseFat16BootSector(_ data: [UInt8], baseLBA: UInt32) -> Fat16Volume? {
    guard let bpb = fatParseBPB(data, baseLBA: baseLBA) else { return nil }
    if bpb.fatType != .fat16 { return nil }
    return Fat16Volume(bytesPerSector: bpb.bytesPerSector,
                       sectorsPerCluster: bpb.sectorsPerCluster,
                       reservedSectors: bpb.reservedSectors,
                       numFATs: bpb.numFATs,
                       sectorsPerFAT: bpb.sectorsPerFAT,
                       totalSectors: bpb.totalSectors,
                       baseLBA: bpb.baseLBA,
                       fatStartLBA: bpb.fatStartLBA,
                       dataStartLBA: bpb.dataStartLBA,
                       rootDirStartLBA: bpb.rootDirStartLBA,
                       rootDirSectors: bpb.rootDirSectors,
                       walStartLBA: 0,
                       walSectors: 0)
}

private func configureWalRegion(_ vol: Fat16Volume) -> Fat16Volume {
    let walSectors: UInt32 = 2
    if vol.reservedSectors < 16 || UInt32(vol.reservedSectors) <= walSectors + 8 {
        return Fat16Volume(bytesPerSector: vol.bytesPerSector,
                           sectorsPerCluster: vol.sectorsPerCluster,
                           reservedSectors: vol.reservedSectors,
                           numFATs: vol.numFATs,
                           sectorsPerFAT: vol.sectorsPerFAT,
                           totalSectors: vol.totalSectors,
                           baseLBA: vol.baseLBA,
                           fatStartLBA: vol.fatStartLBA,
                           dataStartLBA: vol.dataStartLBA,
                           rootDirStartLBA: vol.rootDirStartLBA,
                           rootDirSectors: vol.rootDirSectors,
                           walStartLBA: 0,
                           walSectors: 0)
    }
    let walStart = vol.baseLBA + UInt32(vol.reservedSectors) - walSectors
    if walStart <= vol.baseLBA + 8 {
        return Fat16Volume(bytesPerSector: vol.bytesPerSector,
                           sectorsPerCluster: vol.sectorsPerCluster,
                           reservedSectors: vol.reservedSectors,
                           numFATs: vol.numFATs,
                           sectorsPerFAT: vol.sectorsPerFAT,
                           totalSectors: vol.totalSectors,
                           baseLBA: vol.baseLBA,
                           fatStartLBA: vol.fatStartLBA,
                           dataStartLBA: vol.dataStartLBA,
                           rootDirStartLBA: vol.rootDirStartLBA,
                           rootDirSectors: vol.rootDirSectors,
                           walStartLBA: 0,
                           walSectors: 0)
    }
    return Fat16Volume(bytesPerSector: vol.bytesPerSector,
                       sectorsPerCluster: vol.sectorsPerCluster,
                       reservedSectors: vol.reservedSectors,
                       numFATs: vol.numFATs,
                       sectorsPerFAT: vol.sectorsPerFAT,
                       totalSectors: vol.totalSectors,
                       baseLBA: vol.baseLBA,
                       fatStartLBA: vol.fatStartLBA,
                       dataStartLBA: vol.dataStartLBA,
                       rootDirStartLBA: vol.rootDirStartLBA,
                       rootDirSectors: vol.rootDirSectors,
                       walStartLBA: walStart,
                       walSectors: walSectors)
}

private func walReplayIfNeeded(_ block: BlockDevice, _ vol: Fat16Volume) {
    if vol.walStartLBA == 0 || vol.walSectors < 2 { return }
    guard let header = block.read(lba: UInt64(vol.walStartLBA), count: 1, intent: IO_INTENT_THROUGHPUT) else { return }
    if header.count < 16 { return }
    if header[0] != 0x57 || header[1] != 0x41 || header[2] != 0x4C || header[3] != 0x31 { return }
    let state = readU32LE(header, 4)
    if state != 1 { return }
    let target = readU64LE(header, 8)
    guard let data = block.read(lba: UInt64(vol.walStartLBA + 1), count: 1, intent: IO_INTENT_THROUGHPUT) else { return }
    _ = block.write(lba: target, count: 1, data: data, intent: IO_INTENT_THROUGHPUT)
    let clear = [UInt8](repeating: 0, count: Int(vol.bytesPerSector))
    _ = block.write(lba: UInt64(vol.walStartLBA), count: 1, data: clear, intent: IO_INTENT_THROUGHPUT)
}

private func walWriteSector(_ block: BlockDevice, _ vol: Fat16Volume, targetLBA: UInt64, data: [UInt8], simulateCrash: Bool) -> Bool {
    if vol.walStartLBA == 0 || vol.walSectors < 2 {
        return block.write(lba: targetLBA, count: 1, data: data, intent: IO_INTENT_THROUGHPUT)
    }
    let sectorSize = Int(vol.bytesPerSector)
    if data.count < sectorSize { return false }
    var header = [UInt8](repeating: 0, count: sectorSize)
    header[0] = 0x57; header[1] = 0x41; header[2] = 0x4C; header[3] = 0x31
    writeU32LE(1, &header, 4)
    let t = targetLBA
    for i in 0..<8 {
        header[8 + i] = UInt8(truncatingIfNeeded: t >> (UInt64(i) * 8))
    }
    if !block.write(lba: UInt64(vol.walStartLBA), count: 1, data: header, intent: IO_INTENT_THROUGHPUT) {
        return false
    }
    if !block.write(lba: UInt64(vol.walStartLBA + 1), count: 1, data: Array(data[0..<sectorSize]), intent: IO_INTENT_THROUGHPUT) {
        return false
    }
    if simulateCrash {
        return false
    }
    if !block.write(lba: targetLBA, count: 1, data: Array(data[0..<sectorSize]), intent: IO_INTENT_THROUGHPUT) {
        return false
    }
    let clear = [UInt8](repeating: 0, count: sectorSize)
    _ = block.write(lba: UInt64(vol.walStartLBA), count: 1, data: clear, intent: IO_INTENT_THROUGHPUT)
    return true
}

private func validateFat16Volume(_ block: BlockDevice, _ vol: Fat16Volume) -> Bool {
    if vol.bytesPerSector == 0 || vol.sectorsPerCluster == 0 || vol.sectorsPerFAT == 0 {
        return false
    }
    if UInt32(block.sectorSize) != UInt32(vol.bytesPerSector) {
        return false
    }
    let cap = UInt64(block.capacitySectors)
    let base = UInt64(vol.baseLBA)
    let total = UInt64(vol.totalSectors)
    if total == 0 || base + total > cap {
        return false
    }
    let fatStart = UInt64(vol.fatStartLBA)
    let fatEnd = fatStart + UInt64(vol.sectorsPerFAT) * UInt64(vol.numFATs)
    let dataStart = UInt64(vol.dataStartLBA)
    let rootStart = UInt64(vol.rootDirStartLBA)
    let rootEnd = rootStart + UInt64(vol.rootDirSectors)
    if fatStart < base || fatEnd > base + total { return false }
    if rootStart < base || rootEnd > base + total { return false }
    if dataStart < base || dataStart >= base + total { return false }
    return true
}

private func mountFat16(_ block: BlockDevice) -> Fat16Volume? {
    guard let boot0 = block.read(lba: 0, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if let vol = parseFat16BootSector(boot0, baseLBA: 0) {
        let out = configureWalRegion(vol)
        if !validateFat16Volume(block, out) { return nil }
        walReplayIfNeeded(block, out)
        return out
    }
    if let gptLBA = fatFindPartitionLBAFromGPT(block) {
        if gptLBA <= UInt64(UInt32.max),
           let boot = block.read(lba: gptLBA, count: 1, intent: IO_INTENT_THROUGHPUT) {
            if let vol = parseFat16BootSector(boot, baseLBA: UInt32(gptLBA)) {
                let out = configureWalRegion(vol)
                if !validateFat16Volume(block, out) { return nil }
                walReplayIfNeeded(block, out)
                return out
            }
        }
    }
    guard let partLBA = fatFindPartitionLBAFromMBR(boot0, types: [0x04, 0x06, 0x0E]) else {
        return nil
    }
    guard let boot = block.read(lba: UInt64(partLBA), count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    if let vol = parseFat16BootSector(boot, baseLBA: partLBA) {
        let out = configureWalRegion(vol)
        if !validateFat16Volume(block, out) { return nil }
        walReplayIfNeeded(block, out)
        return out
    }
    return nil
}

private func fat16MaxCluster(_ vol: Fat16Volume) -> UInt32 {
    let dataSectors = Int64(vol.totalSectors) - Int64(vol.dataStartLBA - vol.baseLBA)
    if dataSectors <= 0 { return 0 }
    let clusters = UInt64(dataSectors) / UInt64(vol.sectorsPerCluster)
    if clusters == 0 { return 0 }
    return UInt32(clusters + 1)
}

private func readFat16FAT(_ block: BlockDevice, _ vol: Fat16Volume) -> [UInt8]? {
    if vol.sectorsPerFAT == 0 { return nil }
    let cap = UInt64(block.capacitySectors)
    let fatStart = UInt64(vol.fatStartLBA)
    let fatSectors = UInt64(vol.sectorsPerFAT)
    if fatStart + fatSectors > cap {
        return nil
    }
    let sig = fat16VolumeSig(vol)
    let expectedBytes = Int(vol.sectorsPerFAT) * Int(vol.bytesPerSector)
    if gFat16FatCacheSig != sig || gFat16FatCache.count != expectedBytes {
        gFat16FatCache = [UInt8](repeating: 0, count: expectedBytes)
        gFat16FatCacheSig = sig
    }
    var offset = 0
    for i in 0..<vol.sectorsPerFAT {
        if let part = block.read(lba: UInt64(vol.fatStartLBA + i), count: 1, intent: IO_INTENT_THROUGHPUT) {
            if part.count != Int(vol.bytesPerSector) {
                return nil
            }
            gFat16FatCache.replaceSubrange(offset..<(offset + part.count), with: part)
            offset += part.count
        } else {
            return nil
        }
    }
    return gFat16FatCache
}

private func readFat16RootDirEntries(_ block: BlockDevice, _ vol: Fat16Volume) -> [FatDirEntry]? {
    if vol.rootDirSectors == 0 { return [] }
    guard let data = block.read(lba: UInt64(vol.rootDirStartLBA), count: vol.rootDirSectors, intent: IO_INTENT_THROUGHPUT) else {
        return nil
    }
    var entries: [FatDirEntry] = []
    let res = fatScanDirEntriesEx(data, fatType: .fat16, includeLFN: true)
    for rec in res.records {
        entries.append(rec.entry)
    }
    return entries
}

private func readFat16DirEntriesCluster(_ block: BlockDevice, _ vol: Fat16Volume, _ fat: [UInt8], startCluster: UInt32) -> [FatDirEntry]? {
    let maxCluster = fat16MaxCluster(vol)
    guard let chain = fatClusterChain(fatType: .fat16, fat: fat, start: startCluster, maxCluster: maxCluster) else {
        return nil
    }
    var entries: [FatDirEntry] = []
    for cluster in chain {
        guard let data = readFat16Cluster(block, vol, cluster, intent: IO_INTENT_THROUGHPUT) else {
            return nil
        }
        let res = fatScanDirEntriesEx(data, fatType: .fat16, includeLFN: true)
        for rec in res.records {
            entries.append(rec.entry)
        }
        if res.endReached {
            break
        }
    }
    return entries
}

private func readFat16Cluster(_ block: BlockDevice, _ vol: Fat16Volume, _ cluster: UInt32, intent: UInt32) -> [UInt8]? {
    if cluster < 2 { return nil }
    let lba = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
    return block.read(lba: lba, count: UInt32(vol.sectorsPerCluster), intent: intent)
}

private func findFat16EntryInDir(_ block: BlockDevice, _ vol: Fat16Volume, _ fat: [UInt8], dirCluster: UInt32?, name: [UInt8]) -> FatDirEntry? {
    let entries: [FatDirEntry]?
    if let cluster = dirCluster {
        entries = readFat16DirEntriesCluster(block, vol, fat, startCluster: cluster)
    } else {
        entries = readFat16RootDirEntries(block, vol)
    }
    guard let list = entries else { return nil }
    for entry in list {
        if entry.name == name {
            return entry
        }
    }
    return nil
}

private func resolveFat16PathEntry(_ block: BlockDevice, _ vol: Fat16Volume, _ fat: [UInt8], _ path: NormalizedPath) -> FatDirEntry? {
    if path.isRoot || path.segments.isEmpty {
        return nil
    }
    var dirCluster: UInt32? = nil
    for i in 0..<path.segments.count {
        guard let entry = findFat16EntryInDir(block, vol, fat, dirCluster: dirCluster, name: path.segments[i]) else {
            return nil
        }
        if i == path.segments.count - 1 {
            return entry
        }
        if !entry.isDir || entry.firstCluster < 2 {
            return nil
        }
        dirCluster = entry.firstCluster
    }
    return nil
}

func fat16ReadFile(path: NormalizedPath, intent: UInt32) -> [UInt8]? {
    var info = KsBlockInfo(sector_size: 0, _pad0: 0, capacity_sectors: 0)
    if cka_block_get_info(&info) != KS_STATUS_OK {
        return nil
    }
    let block = BlockDevice(sectorSize: info.sector_size, capacitySectors: info.capacity_sectors)
    guard let vol = mountFat16(block) else { return nil }
    guard let fat = readFat16FAT(block, vol) else { return nil }
    guard let entry = resolveFat16PathEntry(block, vol, fat, path) else { return nil }
    if entry.isDir { return nil }
    if entry.firstCluster < 2 && entry.fileSize > 0 {
        return nil
    }

    let maxCluster = fat16MaxCluster(vol)
    var chain: [UInt32] = []
    if entry.fileSize > 0 {
        guard let chainList = fatClusterChain(fatType: .fat16, fat: fat, start: entry.firstCluster, maxCluster: maxCluster) else {
            return nil
        }
        chain = chainList
    }

    var out: [UInt8] = []
    out.reserveCapacity(Int(entry.fileSize))
    var remaining = Int(entry.fileSize)
    for cluster in chain {
        if remaining <= 0 { break }
        guard let data = readFat16Cluster(block, vol, cluster, intent: intent) else {
            return nil
        }
        if data.isEmpty { break }
        let take = min(remaining, data.count)
        out.append(contentsOf: data[0..<take])
        remaining -= take
    }
    return out
}

private func fat16SetCluster(_ fat: inout [UInt8], _ cluster: UInt32, _ value: UInt16) {
    let off = Int(cluster * 2)
    if off + 1 >= fat.count { return }
    writeU16LE(value, &fat, off)
}

private func fat16FindFreeClusters(_ vol: Fat16Volume, _ fat: [UInt8], count: Int) -> [UInt32]? {
    if count <= 0 { return [] }
    var result: [UInt32] = []
    let maxCluster = fat16MaxCluster(vol)
    if maxCluster < 2 { return nil }
    for c in 2...Int(maxCluster) {
        let cluster = UInt32(c)
        let val = fatNextCluster(fat, fatType: .fat16, cluster: cluster)
        if val == 0 {
            result.append(cluster)
            if result.count >= count {
                return result
            }
        }
    }
    return nil
}

private struct Fat16DirEntryLocation {
    let cluster: UInt32?
    let entryOffset: Int
}

private func fat16FindEntryOrFreeLocation(_ block: BlockDevice, _ vol: Fat16Volume, _ fat: [UInt8], dirCluster: UInt32?, name: [UInt8]) -> (entry: FatDirEntry?, location: Fat16DirEntryLocation?) {
    let short = fatEncodeShortName(name) ?? []
    var freeLoc: Fat16DirEntryLocation? = nil
    func scanData(_ data: [UInt8], cluster: UInt32?) -> (FatDirEntry?, Bool) {
        var off = 0
        while off + 32 <= data.count {
            let first = data[off]
            if first == 0x00 {
                if freeLoc == nil {
                    freeLoc = Fat16DirEntryLocation(cluster: cluster, entryOffset: off)
                }
                return (nil, true)
            }
            if first == 0xE5 {
                if freeLoc == nil {
                    freeLoc = Fat16DirEntryLocation(cluster: cluster, entryOffset: off)
                }
                off += 32
                continue
            }
            let attr = data[off + 11]
            if attr == 0x0F {
                off += 32
                continue
            }
            if (attr & 0x08) != 0 {
                off += 32
                continue
            }
            if !short.isEmpty {
                let nameBytes = Array(data[off..<(off + 11)])
                if nameBytes == short {
                    let hi = UInt32(readU16LE(data, off + 20))
                    let lo = UInt32(readU16LE(data, off + 26))
                    let firstCluster = (hi << 16) | lo
                    let fileSize = readU32LE(data, off + 28)
                    let entry = FatDirEntry(name: name, attr: attr, firstCluster: firstCluster, fileSize: fileSize)
                    return (entry, false)
                }
            }
            off += 32
        }
        return (nil, false)
    }

    if dirCluster == nil {
        guard let data = block.read(lba: UInt64(vol.rootDirStartLBA), count: vol.rootDirSectors, intent: IO_INTENT_THROUGHPUT) else {
            return (nil, nil)
        }
        let (entry, _) = scanData(data, cluster: nil)
        return (entry, freeLoc)
    }

    let maxCluster = fat16MaxCluster(vol)
    guard let chain = fatClusterChain(fatType: .fat16, fat: fat, start: dirCluster!, maxCluster: maxCluster) else {
        return (nil, nil)
    }
    for cluster in chain {
        guard let data = readFat16Cluster(block, vol, cluster, intent: IO_INTENT_THROUGHPUT) else {
            return (nil, nil)
        }
        let (entry, endReached) = scanData(data, cluster: cluster)
        if entry != nil {
            return (entry, freeLoc)
        }
        if endReached { break }
    }
    return (nil, freeLoc)
}

private func fat16WriteDirEntry(_ block: BlockDevice, _ vol: Fat16Volume, _ loc: Fat16DirEntryLocation, _ entryBytes: [UInt8], safe: Bool, simulateCrash: Bool) -> Bool {
    let sectorSize = Int(vol.bytesPerSector)
    let sectorIndex = loc.entryOffset / sectorSize
    let offsetInSector = loc.entryOffset % sectorSize
    let lba: UInt64
    if let cluster = loc.cluster {
        lba = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster) + UInt64(sectorIndex)
    } else {
        lba = UInt64(vol.rootDirStartLBA) + UInt64(sectorIndex)
    }
    guard var sector = block.read(lba: lba, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return false
    }
    if sector.count < sectorSize { return false }
    for i in 0..<32 {
        sector[offsetInSector + i] = entryBytes[i]
    }
    if safe {
        return walWriteSector(block, vol, targetLBA: lba, data: sector, simulateCrash: simulateCrash)
    }
    return block.write(lba: lba, count: 1, data: sector, intent: IO_INTENT_THROUGHPUT)
}

private func fat16MarkDirEntryDeleted(_ block: BlockDevice, _ vol: Fat16Volume, _ loc: Fat16DirEntryLocation, safe: Bool) -> Bool {
    let sectorSize = Int(vol.bytesPerSector)
    let sectorIndex = loc.entryOffset / sectorSize
    let offsetInSector = loc.entryOffset % sectorSize
    let lba: UInt64
    if let cluster = loc.cluster {
        lba = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster) + UInt64(sectorIndex)
    } else {
        lba = UInt64(vol.rootDirStartLBA) + UInt64(sectorIndex)
    }
    guard var sector = block.read(lba: lba, count: 1, intent: IO_INTENT_THROUGHPUT) else {
        return false
    }
    if sector.count < sectorSize { return false }
    sector[offsetInSector] = 0xE5
    if safe {
        return walWriteSector(block, vol, targetLBA: lba, data: sector, simulateCrash: false)
    }
    return block.write(lba: lba, count: 1, data: sector, intent: IO_INTENT_THROUGHPUT)
}

private func fat16WriteFatTables(_ block: BlockDevice, _ vol: Fat16Volume, _ fat: [UInt8], safe: Bool, simulateCrash: Bool) -> Bool {
    let sectorSize = Int(vol.bytesPerSector)
    let fatSectors = Int(vol.sectorsPerFAT)
    let fatBytes = fatSectors * sectorSize
    if fatBytes == 0 || fat.count < fatBytes { return false }
    for fatIndex in 0..<Int(vol.numFATs) {
        let base = UInt64(vol.fatStartLBA) + UInt64(fatIndex * Int(vol.sectorsPerFAT))
        for s in 0..<fatSectors {
            let off = s * sectorSize
            let sectorData = Array(fat[off..<(off + sectorSize)])
            let lba = base + UInt64(s)
            if safe {
                if !walWriteSector(block, vol, targetLBA: lba, data: sectorData, simulateCrash: simulateCrash) {
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

func fat16WriteFile(path: NormalizedPath, data: [UInt8], safe: Bool, simulateCrash: Bool) -> Bool {
    if path.isRoot { return false }
    guard let block = gBlockDevice else { return false }
    guard let vol = mountFat16(block) else { return false }
    guard var fat = readFat16FAT(block, vol) else { return false }

    // Resolve parent directory.
    var parentCluster: UInt32? = nil
    if path.segments.count > 1 {
        var dirCluster: UInt32? = nil
        for i in 0..<(path.segments.count - 1) {
            guard let entry = findFat16EntryInDir(block, vol, fat, dirCluster: dirCluster, name: path.segments[i]) else { return false }
            if !entry.isDir || entry.firstCluster < 2 { return false }
            dirCluster = entry.firstCluster
        }
        parentCluster = dirCluster
    }

    let name = path.segments.last!
    let (existing, freeLoc) = fat16FindEntryOrFreeLocation(block, vol, fat, dirCluster: parentCluster, name: name)
    var entry = existing
    var location = freeLoc
    if entry == nil {
        guard let loc = location else { return false }
        location = loc
        entry = FatDirEntry(name: name, attr: 0x20, firstCluster: 0, fileSize: 0)
    }
    guard let loc = location, var cur = entry else { return false }
    if cur.isDir { return false }

    let clusterSize = Int(UInt32(vol.bytesPerSector) * UInt32(vol.sectorsPerCluster))
    if clusterSize == 0 { return false }
    let requiredClusters = data.isEmpty ? 0 : (data.count + clusterSize - 1) / clusterSize
    var chain: [UInt32] = []
    if cur.firstCluster >= 2 {
        let maxCluster = fat16MaxCluster(vol)
        if let existingChain = fatClusterChain(fatType: .fat16, fat: fat, start: cur.firstCluster, maxCluster: maxCluster) {
            chain = existingChain
        }
    }

    if requiredClusters > chain.count {
        let need = requiredClusters - chain.count
        guard let extra = fat16FindFreeClusters(vol, fat, count: need) else { return false }
        if chain.isEmpty {
            chain = extra
        } else {
            let last = chain[chain.count - 1]
            fat16SetCluster(&fat, last, UInt16(extra[0]))
            chain.append(contentsOf: extra)
        }
        for i in 0..<extra.count {
            let next: UInt16 = (i + 1 < extra.count) ? UInt16(extra[i + 1]) : 0xFFFF
            fat16SetCluster(&fat, extra[i], next)
        }
    } else if requiredClusters < chain.count {
        // Free extra clusters.
        for i in requiredClusters..<chain.count {
            fat16SetCluster(&fat, chain[i], 0)
        }
        if requiredClusters > 0 {
            fat16SetCluster(&fat, chain[requiredClusters - 1], 0xFFFF)
            chain = Array(chain[0..<requiredClusters])
        } else {
            chain = []
        }
    }

    // Write data clusters.
    var remaining = data.count
    var offset = 0
    for cluster in chain {
        if remaining <= 0 { break }
        let lba = UInt64(vol.dataStartLBA) + UInt64(cluster - 2) * UInt64(vol.sectorsPerCluster)
        for s in 0..<UInt32(vol.sectorsPerCluster) {
            let sectorSize = Int(vol.bytesPerSector)
            var sectorData = [UInt8](repeating: 0, count: sectorSize)
            if remaining > 0 {
                let take = min(sectorSize, remaining)
                sectorData.replaceSubrange(0..<take, with: data[offset..<(offset + take)])
                offset += take
                remaining -= take
            }
            if !block.write(lba: lba + UInt64(s), count: 1, data: sectorData, intent: IO_INTENT_THROUGHPUT) {
                return false
            }
        }
    }

    let firstCluster = chain.isEmpty ? 0 : chain[0]
    cur = FatDirEntry(name: cur.name, attr: cur.attr, firstCluster: firstCluster, fileSize: UInt32(data.count))
    guard let short = fatEncodeShortName(cur.name) else { return false }
    var entryBytes = [UInt8](repeating: 0, count: 32)
    for i in 0..<11 { entryBytes[i] = short[i] }
    entryBytes[11] = cur.attr
    writeU16LE(UInt16(truncatingIfNeeded: (cur.firstCluster >> 16) & 0xFFFF), &entryBytes, 20)
    writeU16LE(UInt16(truncatingIfNeeded: cur.firstCluster & 0xFFFF), &entryBytes, 26)
    writeU32LE(cur.fileSize, &entryBytes, 28)

    if !fat16WriteFatTables(block, vol, fat, safe: safe, simulateCrash: false) {
        return false
    }
    if !fat16WriteDirEntry(block, vol, loc, entryBytes, safe: safe, simulateCrash: simulateCrash) {
        return false
    }
    // Keep the cache coherent after a successful write.
    if !simulateCrash {
        gFat16FatCache = fat
        gFat16FatCacheSig = fat16VolumeSig(vol)
    }
    return !simulateCrash
}

func fat16DeleteFile(path: NormalizedPath, safe: Bool) -> Bool {
    if path.isRoot { return false }
    guard let block = gBlockDevice else { return false }
    guard let vol = mountFat16(block) else { return false }
    guard var fat = readFat16FAT(block, vol) else { return false }

    var parentCluster: UInt32? = nil
    if path.segments.count > 1 {
        var dirCluster: UInt32? = nil
        for i in 0..<(path.segments.count - 1) {
            guard let entry = findFat16EntryInDir(block, vol, fat, dirCluster: dirCluster, name: path.segments[i]) else { return false }
            if !entry.isDir || entry.firstCluster < 2 { return false }
            dirCluster = entry.firstCluster
        }
        parentCluster = dirCluster
    }

    let name = path.segments.last!
    let (entryOpt, locOpt) = fat16FindEntryOrFreeLocation(block, vol, fat, dirCluster: parentCluster, name: name)
    guard let entry = entryOpt, let loc = locOpt else { return false }
    if entry.isDir { return false }
    if entry.firstCluster >= 2 {
        let maxCluster = fat16MaxCluster(vol)
        if let chain = fatClusterChain(fatType: .fat16, fat: fat, start: entry.firstCluster, maxCluster: maxCluster) {
            for c in chain {
                fat16SetCluster(&fat, c, 0)
            }
            if !fat16WriteFatTables(block, vol, fat, safe: safe, simulateCrash: false) {
                return false
            }
        }
    }
    if !fat16MarkDirEntryDeleted(block, vol, loc, safe: safe) {
        return false
    }
    gFat16FatCache = fat
    gFat16FatCacheSig = fat16VolumeSig(vol)
    return true
}

func fat16CrashTest(path: NormalizedPath, data: [UInt8]) -> Bool {
    if !fat16WriteFile(path: path, data: data, safe: true, simulateCrash: true) {
        // Simulated crash: WAL should remain and replay on next mount.
    }
    guard let block = gBlockDevice else { return false }
    guard let vol = mountFat16(block) else { return false }
    walReplayIfNeeded(block, vol)
    guard let readBack = fat16ReadFile(path: path, intent: IO_INTENT_THROUGHPUT) else { return false }
    if readBack.count != data.count { return false }
    for i in 0..<data.count {
        if readBack[i] != data[i] { return false }
    }
    return true
}

func fat16SmokeTest(path: NormalizedPath, expectedSize: UInt32, expectedChecksum: UInt32) -> Bool {
    guard let data = fat16ReadFile(path: path, intent: IO_INTENT_THROUGHPUT) else {
        return false
    }
    if UInt32(data.count) != expectedSize {
        return false
    }
    let sum = fatChecksum32(data)
    return sum == expectedChecksum
}

func listFat16Directory(path: NormalizedPath) {
    guard let block = gBlockDevice else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] no block device\r\n")))
        return
    }
    guard let vol = mountFat16(block) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a FAT16 volume\r\n")))
        return
    }
    guard let fat = readFat16FAT(block, vol) else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read FAT\r\n")))
        return
    }
    let entries: [FatDirEntry]?
    if path.isRoot {
        entries = readFat16RootDirEntries(block, vol)
    } else {
        guard let entry = resolveFat16PathEntry(block, vol, fat, path) else {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] path not found\r\n")))
            return
        }
        if !entry.isDir {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] not a directory\r\n")))
            return
        }
        if entry.firstCluster < 2 {
            gConsole?.writeUserOutput(Array(staticStringBytes("[fs] directory has no cluster\r\n")))
            return
        }
        entries = readFat16DirEntriesCluster(block, vol, fat, startCluster: entry.firstCluster)
    }
    guard let list = entries else {
        gConsole?.writeUserOutput(Array(staticStringBytes("[fs] failed to read directory\r\n")))
        return
    }
    gConsole?.writeUserOutput(Array(staticStringBytes("FAT16 dir:\r\n")))
    for entry in list {
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

func fat16ListDirEntries(path: NormalizedPath) -> [FatDirEntry]? {
    guard let block = gBlockDevice else { return nil }
    guard let vol = mountFat16(block) else { return nil }
    guard let fat = readFat16FAT(block, vol) else { return nil }
    if path.isRoot {
        return readFat16RootDirEntries(block, vol)
    }
    guard let entry = resolveFat16PathEntry(block, vol, fat, path) else { return nil }
    if !entry.isDir { return nil }
    if entry.firstCluster < 2 { return nil }
    return readFat16DirEntriesCluster(block, vol, fat, startCluster: entry.firstCluster)
}

func fat16Stat(path: NormalizedPath) -> FsStat? {
    guard let block = gBlockDevice else { return nil }
    guard let vol = mountFat16(block) else { return nil }
    guard let fat = readFat16FAT(block, vol) else { return nil }
    if path.isRoot {
        return FsStat(size: 0, isDir: true)
    }
    guard let entry = resolveFat16PathEntry(block, vol, fat, path) else { return nil }
    return FsStat(size: entry.fileSize, isDir: entry.isDir)
}

func fat16LocateKernelImage() -> [UInt8]? {
    guard let block = gBlockDevice else { return nil }
    guard let vol = mountFat16(block) else { return nil }
    guard let fat = readFat16FAT(block, vol) else { return nil }
    let candidates: [[UInt8]] = [
        Array(staticStringBytes("KERNEL8.IMG")),
        Array(staticStringBytes("KERNEL7L.IMG")),
        Array(staticStringBytes("KERNEL7.IMG")),
        Array(staticStringBytes("KERNEL.IMG"))
    ]

    if let rootEntries = readFat16RootDirEntries(block, vol) {
        if let found = findKernelName(in: rootEntries, candidates: candidates) {
            return found
        }
        let bootName = Array(staticStringBytes("BOOT"))
        if let bootEntry = findFat16EntryInDir(block, vol, fat, dirCluster: nil, name: bootName),
           bootEntry.isDir,
           bootEntry.firstCluster >= 2,
           let bootEntries = readFat16DirEntriesCluster(block, vol, fat, startCluster: bootEntry.firstCluster),
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
