import SwiftUI

/// The camera as every screen shows it: floating, never wider than 72 % of the page.
/// Before this phone has found one it is there but out of focus, and it sharpens and
/// turns the moment it is.
struct Device: View {
    var mood: DeviceMood = .asleep
    var turn: Int = 0
    var lit: Bool = true

    var body: some View {
        DeviceView(mood: mood, turn: turn)
            .aspectRatio(1.3, contentMode: .fit)
            .containerRelativeFrame(.horizontal) { width, _ in width * 0.72 }
            .blur(radius: lit ? 0 : 12)
            .opacity(lit ? 1 : 0.4)
            .animation(.easeInOut(duration: 0.8), value: lit)
            .accessibilityLabel("Your little camera")
    }
}
