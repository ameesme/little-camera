import SwiftUI

/// The app's own landing page: the camera turning through the three things it does, and the way in.
struct IntroStage: View {
    let model: AppModel
    @State private var beat = 0
    @State private var turns = 0

    private static let beats: [(mood: DeviceMood, line: String)] = [
        (.asleep, "One button. No feed, no likes, no scrolling."),
        (.viewfinder, "Point it and press. That is all it does."),
        (.gallery, "Every picture lands on a little blog that is yours."),
        (.saving, "Only the people you let in ever see it."),
    ]

    private var current: (mood: DeviceMood, line: String) { Self.beats[beat] }

    var body: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 24)
            Wordmark()
            Device(mood: current.mood, turn: turns)
                .padding(.top, 20)
            Text(current.line)
                .font(Theme.lead)
                .multilineTextAlignment(.center)
                .frame(maxWidth: .infinity)
                .padding(.horizontal, Theme.pad)
                .padding(.top, 24)
                .id(beat)
                .transition(.opacity)
            Spacer(minLength: 24)
            Button { model.getStarted() } label: { Caption("Get started") }
                .buttonStyle(BlackButton())
                .card()
        }
        .background(Ground())
        .task {
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(3.2))
                turns += 1
                withAnimation(.easeInOut(duration: 0.3)) { beat = (beat + 1) % Self.beats.count }
            }
        }
    }
}
