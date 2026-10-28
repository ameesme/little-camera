import SwiftUI

/// The app's own landing page: the camera turning through the three things it does, and the way in.
struct IntroStage: View {
    let model: AppModel
    @State private var beat = 0
    @State private var turns = 0
    /// The first seconds are the launch screen: the wordmark and the camera, and nothing to do.
    @State private var settled = false

    private static let beats: [(mood: DeviceMood, line: String)] = [
        (.asleep, "One button. No feed, no likes, no scrolling."),
        (.viewfinder, "Point it and press. That is all it does."),
        (.gallery, "Every picture lands on a little blog that is yours."),
        (.saving, "Only the people you let in ever see it."),
    ]

    /// The line follows the panel rather than leading it: both land as the camera comes round.
    @State private var caption = 0

    var body: some View {
        VStack(spacing: 0) {
            // The wordmark sits at the top of the page; the camera keeps the middle.
            Wordmark()
                .padding(.top, 52)
            Spacer(minLength: 20)
            Device(mood: Self.beats[beat].mood, turn: turns)
            Text(Self.beats[caption].line)
                .font(Theme.lead)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)
                .padding(.horizontal, Theme.pad)
                .padding(.top, 24)
                .id(caption)
                .transition(.opacity)
                .opacity(settled ? 1 : 0)
                .offset(y: settled ? 0 : 14)
            Spacer(minLength: 24)
            Button { model.getStarted() } label: { Caption("Get started") }
                .buttonStyle(BlackButton())
                .card()
                .opacity(settled ? 1 : 0)
                .offset(y: settled ? 0 : 20)
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
