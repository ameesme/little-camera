import CoreGraphics
import SwiftUI

/// The landing page's radial vignette: a pure-white field that falls off towards the
/// corners, thresholded against the same 4x4 Bayer matrix the firmware dithers with,
/// so the dots land on the same lattice as everything else that is drawn here.
struct Ground: View {
    var topOnly = false
    private static let vignette = 0.30
    private static let inner = 0.70

    var body: some View {
        GeometryReader { geo in
            Image(decorative: Ground.render(width: Int(geo.size.width), height: Int(geo.size.height), topOnly: topOnly), scale: 1)
                .resizable()
                .interpolation(.none)
        }
        .ignoresSafeArea()
    }

    private static let bayer4: [Int] = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
    private static let offset: [[Int]] = [[0, 2], [3, 1]]

    private static func threshold(_ x: Int, _ y: Int) -> Double {
        let base = 4 * bayer4[((y & 3) << 2) | (x & 3)]
        return (Double(base + offset[(y >> 2) & 1][(x >> 2) & 1]) + 0.5) / 64
    }

    /// Cached per size: the field only changes when the screen does, and a per-pixel
    /// pass on every frame would cost more than the whole rest of the app.
    nonisolated(unsafe) private static var cache: [String: CGImage] = [:]

    static func render(width: Int, height: Int, topOnly: Bool = false) -> CGImage {
        let w = max(1, width), h = max(1, height)
        let key = "\(w)x\(h)\(topOnly ? "t" : "")"
        if let hit = cache[key] { return hit }

        var pixels = [UInt8](repeating: 255, count: w * h)
        let cx = Double(w) / 2, cy = Double(h) / 2
        let radius = 0.5 * (Double(w) * Double(w) + Double(h) * Double(h)).squareRoot()
        let cutoff = topOnly ? h / 2 : h
        for y in 0..<cutoff {
            for x in 0..<w {
                let t = ((Double(x) - cx) * (Double(x) - cx) + (Double(y) - cy) * (Double(y) - cy)).squareRoot() / radius
                let lum = 1 - vignette * min(1, max(0, (t - inner) / (1 - inner)))
                if lum <= threshold(x, y) { pixels[y * w + x] = 0 }
            }
        }
        let image = pixels.withUnsafeMutableBufferPointer { buffer -> CGImage in
            let context = CGContext(
                data: buffer.baseAddress, width: w, height: h, bitsPerComponent: 8, bytesPerRow: w,
                space: CGColorSpaceCreateDeviceGray(), bitmapInfo: CGImageAlphaInfo.none.rawValue
            )
            return context!.makeImage()!
        }
        cache[key] = image
        return image
    }
}
