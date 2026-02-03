//
//  StringHelpers.swift
//  OS
//
//  Created by Cosas on 2/2/26.
//

@inline(__always)
public func staticStringBytes(_ s: StaticString) -> UnsafeBufferPointer<UInt8> {
    let count = s.utf8CodeUnitCount
    return UnsafeBufferPointer(start: s.utf8Start, count: count)
}


@inline(__always)
func == (lhs: [UInt8], rhs: StaticString) -> Bool {
    let rb = staticStringBytes(rhs)
    if lhs.count != rb.count { return false }
    for i in 0..<rb.count {
        if lhs[i] != rb[i] { return false }
    }
    return true
}

@inline(__always)
public func bytesEqual(_ lhs: [UInt8], _ rhs: StaticString) -> Bool {
    let r = staticStringBytes(rhs)
    if lhs.count != r.count { return false }
    for i in 0..<r.count where lhs[i] != r[i] { return false }
    return true
}
