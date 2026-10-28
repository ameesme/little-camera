import CoreImage
import CoreImage.CIFilterBuiltins
import SwiftUI

/// The verification code as the camera has to see it: `LC:XXXXXX`, error correction H,
/// black on white, drawn without interpolation so the modules stay square (§5).
struct QRView: View {
    let code: String

    var body: some View {
        Group {
            if let image = QRView.render("LC:\(code)") {
                Image(decorative: image, scale: 1)
                    .resizable()
                    .interpolation(.none)
                    .aspectRatio(1, contentMode: .fit)
                    .padding(12)
                    .background(Theme.paper)
            }
        }
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
