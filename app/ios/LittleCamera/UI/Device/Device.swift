import SwiftUI

/// The camera as every screen shows it: floating, never wider than 60 % of the page.
struct Device: View {
    var mood: DeviceMood = .asleep
    var turn: Int = 0
    /// False while there is no camera yet: the outline stands in, and the real one fades up over it.
    var lit: Bool = true

    var body: some View {
        ZStack {
            DeviceOutline()
                .opacity(lit ? 0 : 1)
            DeviceView(mood: mood, turn: turn)
                .opacity(lit ? 1 : 0)
        }
        .aspectRatio(1.3, contentMode: .fit)
        .containerRelativeFrame(.horizontal) { width, _ in width * 0.72 }
        .animation(.easeInOut(duration: 0.6), value: lit)
        .accessibilityLabel("Your little camera")
    }
}

/// 63.6 × 40.6 mm of body around 57.6 × 34.56 mm of glass, as a dithered shadow of the real thing.
private struct DeviceOutline: View {
    var body: some View {
        GeometryReader { geo in
            let width = min(geo.size.width, geo.size.height * 63.6 / 40.6)
            let height = width * 40.6 / 63.6
            let bezel = width * 3 / 63.6
            Dither()
                .mask {
                    RoundedRectangle(cornerRadius: bezel)
                        .fill(Theme.ink)
                        .overlay {
                            RoundedRectangle(cornerRadius: bezel / 2)
                                .padding(bezel)
                                .blendMode(.destinationOut)
                        }
                        .compositingGroup()
                }
                .frame(width: width, height: height)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
    }
}
