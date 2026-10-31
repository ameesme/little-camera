import CoreImage
import CoreImage.CIFilterBuiltins
import SwiftUI

/// The verification code as the camera has to see it: six bare characters, black on white, drawn
/// without interpolation so the modules stay square (§5). Six bytes is exactly what version 1 holds
/// at error correction H, and CoreImage only encodes in byte mode: one character more and the code
/// becomes 25 modules across, which the camera's 320 px cannot spare.
struct QRView: View {
    let code: String

    /// jsQR wants four clear modules around the code, and a card this small has to be told so in points.
    private static let quietModules: CGFloat = 4

    var body: some View {
        GeometryReader { geo in
            if let image = QRView.render(code) {
                let side = min(geo.size.width, geo.size.height)
                let module = side / (CGFloat(image.width) + QRView.quietModules * 2)
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .frame(width: side - module * QRView.quietModules * 2,
                           height: side - module * QRView.quietModules * 2)
                    .frame(width: side, height: side)
                    .background(Theme.paper)
            }
        }
        .aspectRatio(1, contentMode: .fit)
        .accessibilityLabel("QR code \(code)")
    }

    private static func render(_ payload: String) -> CGImage? {
        let filter = CIFilter.qrCodeGenerator()
        filter.message = Data(payload.utf8)
        filter.correctionLevel = "H"
        guard let output = filter.outputImage else { return nil }
        // Inverted, because the generator draws the modules in alpha over nothing.
        let mono = output.applyingFilter("CIColorInvert")
        return CIContext().createCGImage(mono, from: mono.extent)
    }
}
