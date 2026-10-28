import SwiftUI

/// The app's own landing page: the camera turning through what it does, and the way in.
/// It places itself, because the camera has to sit in the middle of the page and the
/// wordmark has to halve the gap above it, and spacers only ever argue about that.
struct IntroStage: View {
    let model: AppModel
    @State private var beat = 0
    @State private var turns = 0
    /// The camera arrives first and alone; everything that names it follows.
    @State private var settled = false
    /// The line follows the panel rather than leading it: both land as the camera comes round.
    @State private var caption = 0

    private static let beats: [(mood: DeviceMood, line: String)] = [
        (.asleep, "One button. No feed, no likes, no scrolling."),
        (.viewfinder, "Point it and press. That is all it does."),
        (.gallery, "Every picture lands on a little blog that is yours."),
        (.saving, "Only the people you let in ever see it."),
    ]

    var body: some View {
        GeometryReader { geo in
            let page = geo.size
            let width = page.width * Device.share
            let height = width / Device.aspect
            // The shader leaves room for the shadow under the body, so the lit camera sits
            // a twentieth of the canvas above its centre. Nudge the canvas down by that,
            // and what the eye calls the camera lands on the middle of the page.
            let lift = height * 0.05
            let middle = page.height / 2 + lift
            // The body starts about a seventh into the canvas, and the gap the eye measures
            // is the one above the body, not above the canvas.
            let top = middle - height / 2 + height * 0.15

            ZStack {
                Wordmark()
                    .opacity(settled ? 1 : 0)
                    .offset(y: settled ? 0 : -12)
                    .position(x: page.width / 2, y: top / 2)

                Device(mood: Self.beats[beat].mood, turn: turns, width: width)
                    .position(x: page.width / 2, y: middle)

                Text(Self.beats[caption].line)
                    .font(Theme.lead)
                    .multilineTextAlignment(.center)
                    .frame(width: page.width - Theme.pad * 2)
                    .id(caption)
                    .transition(.opacity)
                    .opacity(settled ? 1 : 0)
                    .offset(y: settled ? 0 : 14)
                    .position(x: page.width / 2, y: middle + height / 2 + 36)

                Button { model.getStarted() } label: { Caption("Get started") }
                    .buttonStyle(BlackButton())
                    .frame(width: page.width - Theme.pad * 2)
                    .opacity(settled ? 1 : 0)
                    .offset(y: settled ? 0 : 20)
                    .position(x: page.width / 2, y: page.height - 39)
            }
        }
        .background(Ground())
        .task {
            try? await Task.sleep(for: .seconds(1.5))
            withAnimation(.easeOut(duration: 0.6)) { settled = true }
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(3.2))
                beat = (beat + 1) % Self.beats.count
                turns += 1
                // Half a turn, which is where camera-element.js swaps the panel.
                try? await Task.sleep(for: .milliseconds(495))
                withAnimation(.easeInOut(duration: 0.3)) { caption = beat }
            }
        }
    }
}
