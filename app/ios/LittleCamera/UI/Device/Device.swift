import SwiftUI

/// The camera as every screen shows it: floating, 72 % of the page wide unless a screen
/// that lays itself out says otherwise. Before this phone has found one it is there but
/// out of focus, and it sharpens and turns the moment it is.
struct Device: View {
    static let aspect: CGFloat = 1.15
    static let share: CGFloat = 0.72

    var mood: DeviceMood = .asleep
    var turn: Int = 0
    var lit: Bool = true
    var width: CGFloat?

    var body: some View {
        Group {
            if let width {
                panel.frame(width: width, height: width / Device.aspect)
            } else {
                panel
                    .aspectRatio(Device.aspect, contentMode: .fit)
                    .containerRelativeFrame(.horizontal) { page, _ in page * Device.share }
            }
        }
        .accessibilityLabel("Your little camera")
    }

    private var panel: some View {
        DeviceView(mood: mood, turn: turn)
            .blur(radius: lit ? 0 : 12)
            .opacity(lit ? 1 : 0.4)
            .animation(.easeInOut(duration: 0.8), value: lit)
    }
}
