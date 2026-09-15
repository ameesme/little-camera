import Foundation
import CoreBluetooth
import CryptoKit

// Byte-level contract with the camera. Everything here is written from
// docs/protocol.md; change that file first.

/// GATT identifiers (protocol §3). Base UUID `1C0000xx-4C43-4D52-8000-6C6974746C65`.
enum LC {
    static let serviceUUID = CBUUID(string: "1C000001-4C43-4D52-8000-6C6974746C65")
    static let infoUUID = CBUUID(string: "1C000002-4C43-4D52-8000-6C6974746C65")
    static let secretUUID = CBUUID(string: "1C000003-4C43-4D52-8000-6C6974746C65")
    static let controlUUID = CBUUID(string: "1C000004-4C43-4D52-8000-6C6974746C65")
    static let dataUUID = CBUUID(string: "1C000005-4C43-4D52-8000-6C6974746C65")
    static let characteristicUUIDs: [CBUUID] = [infoUUID, secretUUID, controlUUID, dataUUID]
}

enum ProtocolError: LocalizedError {
    case shortData(String)
    case badVersion(UInt8)
    case badFrame

    var errorDescription: String? {
        switch self {
        case .shortData(let what): return "\(what) is too short"
        case .badVersion(let v): return "Info version \(v) not understood"
        case .badFrame: return "malformed frame"
        }
    }
}

// MARK: - Little-endian helpers

extension Data {
    // Offsets are relative to the slice, not to the underlying buffer: Data slices
    // keep their parent's indices, which is a classic way to read the wrong byte.
    func lcByte(_ offset: Int) -> UInt8 {
        self[startIndex + offset]
    }

    func lcU16(_ offset: Int) -> UInt16 {
        UInt16(lcByte(offset)) | (UInt16(lcByte(offset + 1)) << 8)
    }

    func lcU32(_ offset: Int) -> UInt32 {
        UInt32(lcU16(offset)) | (UInt32(lcU16(offset + 2)) << 16)
    }

    mutating func lcAppend(_ value: UInt16) {
        append(UInt8(value & 0xFF))
        append(UInt8(value >> 8))
    }

    mutating func lcAppend(_ value: UInt32) {
        lcAppend(UInt16(value & 0xFFFF))
        lcAppend(UInt16(value >> 16))
    }

    var hexString: String {
        map { String(format: "%02x", $0) }.joined()
    }
}

// MARK: - Info (§3.1)

struct CameraInfo: Equatable {
    static let byteCount = 25

    let version: UInt8
    let mac: [UInt8]
    let photoCount: UInt16
    let unsyncedCount: UInt16
    let newestIndex: UInt16
    let boot: UInt16
    let uptimeMs: UInt32
    let epoch: UInt32
    let flags: UInt8

    var timeValid: Bool { flags & 0x01 != 0 }
    var storageOK: Bool { flags & 0x02 != 0 }
    var busy: Bool { flags & 0x04 != 0 }

    /// 12 lowercase hex characters, the same bytes as the Bluetooth MAC (§1).
    var cameraId: String { Data(mac).hexString }
    /// `lc-` + last four hex characters, uppercase (§1).
    var deviceName: String { "lc-" + cameraId.suffix(4).uppercased() }

    init(data: Data) throws {
        guard data.count >= CameraInfo.byteCount else { throw ProtocolError.shortData("Info") }
        let v = data.lcByte(0)
        guard v == 1 else { throw ProtocolError.badVersion(v) }
        version = v
        mac = (1...6).map { data.lcByte($0) }
        photoCount = data.lcU16(7)
        unsyncedCount = data.lcU16(9)
        newestIndex = data.lcU16(11)
        boot = data.lcU16(13)
        uptimeMs = data.lcU32(15)
        epoch = data.lcU32(19)
        flags = data.lcByte(23)
    }
}

// MARK: - Control (§3.3)

enum ControlOp {
    case setTime(epoch: UInt32)
    case list(from: UInt16, unsyncedOnly: Bool)
    case get(index: UInt16, offset: UInt32)
    case ack(index: UInt16)
    case delete(index: UInt16)
    case abort

    var code: UInt8 {
        switch self {
        case .setTime: return 0x01
        case .list: return 0x02
        case .get: return 0x03
        case .ack: return 0x04
        case .delete: return 0x05
        case .abort: return 0x06
        }
    }

    var name: String {
        switch self {
        case .setTime: return "SET_TIME"
        case .list: return "LIST"
        case .get: return "GET"
        case .ack: return "ACK"
        case .delete: return "DELETE"
        case .abort: return "ABORT"
        }
    }

    /// `[u8 op][args…]`, little-endian.
    var encoded: Data {
        var d = Data([code])
        switch self {
        case .setTime(let epoch):
            d.lcAppend(epoch)
        case .list(let from, let unsyncedOnly):
            d.lcAppend(from)
            d.append(unsyncedOnly ? 0x01 : 0x00)
        case .get(let index, let offset):
            d.lcAppend(index)
            d.lcAppend(offset)
        case .ack(let index), .delete(let index):
            d.lcAppend(index)
        case .abort:
            break
        }
        return d
    }
}

// MARK: - Data frames (§3.4)

enum FrameKind: UInt8 {
    case listData = 0x01
    case photoData = 0x02
    case end = 0x7F
}

/// `[u8 kind][u16 seq][payload]`
struct DataFrame {
    let kind: FrameKind
    let seq: UInt16
    let payload: Data

    init?(data: Data) {
        guard data.count >= 3, let k = FrameKind(rawValue: data.lcByte(0)) else { return nil }
        kind = k
        seq = data.lcU16(1)
        // Copy out of the characteristic's buffer so the frame owns its bytes.
        payload = Data(data.dropFirst(3))
    }
}

enum EndStatus: UInt8 {
    case ok = 0
    case notFound = 1
    case aborted = 2
    case storageError = 3
    case busy = 4

    var label: String {
        switch self {
        case .ok: return "ok"
        case .notFound: return "not found"
        case .aborted: return "aborted"
        case .storageError: return "storage error"
        case .busy: return "busy"
        }
    }
}

/// END payload: `u8 op, u8 status, u32 total_len, u32 crc32`.
struct EndFrame {
    static let byteCount = 10

    let op: UInt8
    let status: UInt8
    let totalLength: UInt32
    let crc32: UInt32

    var statusLabel: String { EndStatus(rawValue: status)?.label ?? "status \(status)" }

    init(payload: Data) throws {
        guard payload.count >= EndFrame.byteCount else { throw ProtocolError.shortData("END") }
        op = payload.lcByte(0)
        status = payload.lcByte(1)
        totalLength = payload.lcU32(2)
        crc32 = payload.lcU32(6)
    }
}

/// LIST_DATA entry, 16 bytes.
struct ListEntry {
    static let byteCount = 16

    let index: UInt16
    let flags: UInt8
    let size: UInt32
    let epoch: UInt32
    let uptimeMs: UInt32

    var synced: Bool { flags & 0x01 != 0 }
    var epochIsEstimate: Bool { flags & 0x02 != 0 }
    var epochFromClock: Bool { flags & 0x04 != 0 }

    init(data: Data) throws {
        guard data.count >= ListEntry.byteCount else { throw ProtocolError.shortData("LIST entry") }
        index = data.lcU16(0)
        flags = data.lcByte(2)
        size = data.lcU32(4)
        epoch = data.lcU32(8)
        uptimeMs = data.lcU32(12)
    }

    /// Parses the concatenated payload of every LIST_DATA frame of one stream.
    static func parseAll(_ payload: Data) throws -> [ListEntry] {
        guard payload.count % ListEntry.byteCount == 0 else { throw ProtocolError.badFrame }
        var entries: [ListEntry] = []
        var offset = 0
        while offset + ListEntry.byteCount <= payload.count {
            let slice = payload.subdata(in: (payload.startIndex + offset)..<(payload.startIndex + offset + ListEntry.byteCount))
            entries.append(try ListEntry(data: slice))
            offset += ListEntry.byteCount
        }
        return entries
    }
}

// MARK: - CRC-32 (IEEE 802.3, reflected, as in zlib)

enum CRC32 {
    private static let table: [UInt32] = (0..<256).map { i -> UInt32 in
        var c = UInt32(i)
        for _ in 0..<8 {
            c = (c & 1) != 0 ? (0xEDB8_8320 ^ (c >> 1)) : (c >> 1)
        }
        return c
    }

    static func checksum(_ data: Data) -> UInt32 {
        var c: UInt32 = 0xFFFF_FFFF
        for byte in data {
            c = table[Int((c ^ UInt32(byte)) & 0xFF)] ^ (c >> 8)
        }
        return c ^ 0xFFFF_FFFF
    }
}

// MARK: - Short code (§1)

enum ShortCode {
    /// 32 characters, no `0 O 1 I`.
    static let alphabet: [Character] = Array("ABCDEFGHJKLMNPQRSTUVWXYZ23456789")

    /// Test vectors (shared with the firmware and the server):
    ///   7cdfa1e2b3c4 → MM48F3
    ///   000000000000 → 882TLC
    ///   ffffffffffff → XMPHSF
    ///   a1b2c3d4e5f6 → ZZWB7E
    static func compute(_ cameraId: String) -> String {
        let hash = Array(SHA256.hash(data: Data(cameraId.utf8)))
        // First 40 bits, big-endian; six 5-bit groups from the top.
        var v: UInt64 = 0
        for i in 0..<5 {
            v = (v << 8) | UInt64(hash[i])
        }
        var code = ""
        for i in 0..<6 {
            let shift = UInt64(35 - 5 * i)
            code.append(alphabet[Int((v >> shift) & 31)])
        }
        return code
    }
}
