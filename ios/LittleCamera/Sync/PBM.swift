import Foundation
import CoreGraphics

/// Binary PBM (P4) as the firmware writes it (protocol §2): `P4`, an optional
/// `# boot= up= t=` comment, width, height, one whitespace byte, packed rows.
struct PBMHeader {
    var width = 0
    var height = 0
    /// Boot counter at capture; nil for files from older firmware (no comment line).
    var boot: UInt16?
    /// `millis()` at capture.
    var up: UInt32?
    /// Unix time at capture, 0 when the camera did not know the time.
    var t: UInt32?
    /// Where the raster starts.
    var rasterOffset = 0

    var rowBytes: Int { (width + 7) / 8 }
    var rasterLength: Int { rowBytes * height }
}

enum PBMError: LocalizedError {
    case badHeader
    case truncated

    var errorDescription: String? {
        switch self {
        case .badHeader: return "not a P4 PBM"
        case .truncated: return "PBM shorter than its header says"
        }
    }
}

enum PBM {
    static let width = 320
    static let height = 240

    private static func isSpace(_ b: UInt8) -> Bool {
        b == 0x20 || b == 0x09 || b == 0x0A || b == 0x0D || b == 0x0B || b == 0x0C
    }

    static func parse(_ data: Data) throws -> PBMHeader {
        let bytes = [UInt8](data)
        var header = PBMHeader()
        var tokens: [String] = []
        var i = 0
        // Three tokens: magic, width, height. `#` comments may sit anywhere between them.
        while tokens.count < 3 && i < bytes.count {
            let b = bytes[i]
            if isSpace(b) {
                i += 1
                continue
            }
            if b == 0x23 {
                var j = i + 1
                while j < bytes.count && bytes[j] != 0x0A && bytes[j] != 0x0D {
                    j += 1
                }
                parseComment(String(decoding: bytes[(i + 1)..<j], as: UTF8.self), into: &header)
                i = j
                continue
            }
            var j = i
            while j < bytes.count && !isSpace(bytes[j]) && bytes[j] != 0x23 {
                j += 1
            }
            tokens.append(String(decoding: bytes[i..<j], as: UTF8.self))
            i = j
        }
        guard tokens.count == 3, tokens[0] == "P4",
              let w = Int(tokens[1]), let h = Int(tokens[2]), w > 0, h > 0 else {
            throw PBMError.badHeader
        }
        // Exactly one whitespace byte separates the height from the raster.
        guard i < bytes.count, isSpace(bytes[i]) else { throw PBMError.truncated }
        i += 1
        header.width = w
        header.height = h
        header.rasterOffset = i
        guard bytes.count - i >= header.rasterLength else { throw PBMError.truncated }
        return header
    }

    /// `boot=17 up=48213 t=1757789000`. Unknown keys are ignored.
    private static func parseComment(_ comment: String, into header: inout PBMHeader) {
        for pair in comment.split(separator: " ") {
            let kv = pair.split(separator: "=", maxSplits: 1)
            guard kv.count == 2 else { continue }
            switch kv[0] {
            case "boot": header.boot = UInt16(kv[1])
            case "up": header.up = UInt32(kv[1])
            case "t": header.t = UInt32(kv[1])
            default: break
            }
        }
    }

    /// 8-bit grey CGImage of the raster (set bit = black). Meant for thumbnails:
    /// draw it with `.interpolation(.none)` so the dither stays crisp.
    static func image(_ data: Data) -> CGImage? {
        guard let header = try? parse(data) else { return nil }
        let w = header.width
        let h = header.height
        let rowBytes = header.rowBytes
        var grey = [UInt8](repeating: 255, count: w * h)
        data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
            for y in 0..<h {
                let row = header.rasterOffset + y * rowBytes
                for x in 0..<w {
                    let byte = raw[row + x / 8]
                    if byte & (UInt8(0x80) >> UInt8(x % 8)) != 0 {
                        grey[y * w + x] = 0
                    }
                }
            }
        }
        guard let provider = CGDataProvider(data: Data(grey) as CFData) else { return nil }
        return CGImage(
            width: w,
            height: h,
            bitsPerComponent: 8,
            bitsPerPixel: 8,
            bytesPerRow: w,
            space: CGColorSpaceCreateDeviceGray(),
            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue),
            provider: provider,
            decode: nil,
            shouldInterpolate: false,
            intent: .defaultIntent
        )
    }
}
