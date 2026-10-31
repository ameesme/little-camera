import SwiftUI

/// The app's own landing page: the camera turning through what it does, and the way in.
/// It places itself, because the camera has to sit in the middle of the page and the
/// wordmark has to halve the gap above it, and spacers only ever argue about that.
struct IntroStage: View {
    let model: AppModel
    @State private var beat = 0
    @State private var turns = 0
    @State private var settled = false
    @State private var lined = false
    /// The line follows the panel rather than leading it: both land as the camera comes round.
    @State private var caption = 0

    private static let firstLine = 0.3
    private static let hold = 2.705
    /// Where camera-element.js swaps the panel, so the words change edge-on rather than in view.
    private static let halfTurn = 0.495

    private static let beats: [(mood: DeviceMood, phone: Bool, line: String)] = [
        (.asleep, false, "Say hi to your little camera"),
        (.viewfinder, false, "Little camera takes beautiful black-and-white pictures"),
        (.blog, true, "It posts them to your little blog for all your friends to see"),
    ]

    var body: some View {
        GeometryReader { geo in
            let page = geo.size
            let width = page.width * Device.share
            // Everything below is measured against the camera's own box; the canvas around it is
            // taller than that so the phone it morphs into has somewhere to be.
            let height = width / Device.aspect
            let canvas = width / 0.8
            // The shader leaves room for the shadow under the body, so the lit camera sits
            // a twentieth of the canvas above its centre. Nudge the canvas down by that,
            // and what the eye calls the camera lands on the middle of the page.
            let lift = height * 0.05
            // Off centre by a tenth of the camera, which is what the line and the button below it live on.
            let middle = page.height / 2 + lift - height * 0.12
            // The body starts about a seventh into the canvas, and the gap the eye measures
            // is the one above the body, not above the canvas.
            let top = middle - height / 2 + height * 0.15

            ZStack {
                Wordmark()
                    .opacity(settled ? 1 : 0)
                    .offset(y: settled ? 0 : -12)
                    .position(x: page.width / 2, y: top / 2 - height * 0.08)

                Device(mood: Self.beats[beat].mood, turn: turns, phone: Self.beats[beat].phone, aspect: 0.8, width: width)
                    .frame(height: canvas)
                    .position(x: page.width / 2, y: middle)

                if lined {
                    // One place for all three, clear of the phone, which reaches further down the canvas than the camera does.
                    Line(text: Self.beats[caption].line, width: page.width - Theme.pad * 2)
                        .id(caption)
                        .position(x: page.width / 2, y: middle + canvas * 0.45 + 72)
                }

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
            try? await Task.sleep(for: .seconds(IntroStage.firstLine))
            lined = true
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(IntroStage.hold + (beat == Self.beats.count - 1 ? 0.5 : 0)))
                beat = (beat + 1) % Self.beats.count
                turns += 1
                try? await Task.sleep(for: .seconds(IntroStage.halfTurn))
                caption = beat
            }
        }
        .task {
            try? await Task.sleep(for: .seconds(0.9))
            withAnimation(.easeOut(duration: 0.6)) { settled = true }
        }
    }
}

/// One line of the intro, whose words land one at a time. A view of its own so that `.id` hands
/// each line a reveal that starts at nothing: setting a shared one back to 0 and animating it away
/// again in the same breath is one update, and one update does not animate.
private struct Line: View {
    let text: String
    let width: CGFloat
    @State private var reveal = 0.0

    var body: some View {
        Text.words(text)
            .font(Theme.medium(24))
            .lineSpacing(4)
            .foregroundStyle(Theme.paper)
            .textRenderer(Inverted(progress: reveal))
            .multilineTextAlignment(.center)
            .frame(width: width)
            .onAppear { withAnimation(.easeOut(duration: 0.8)) { reveal = 1 } }
    }
}
