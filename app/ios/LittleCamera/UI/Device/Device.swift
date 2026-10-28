import SwiftUI

/// The camera as every screen shows it: floating, never wider than 60 % of the page.
struct Device: View {
    var mood: DeviceMood = .asleep
    var celebrate: Int = 0

    var body: some View {
        DeviceView(mood: mood, celebrate: celebrate)
            .aspectRatio(1.3, contentMode: .fit)
            .containerRelativeFrame(.horizontal) { width, _ in width * 0.6 }
            .accessibilityLabel("Your little camera")
    }
}
