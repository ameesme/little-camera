import SwiftUI

/// One stage at a time, and the tabs once there is a blog to show.
struct RootView: View {
    let model: AppModel
    @State private var splash = true

    var body: some View {
        ZStack {
            Group {
                switch model.stage {
                case .intro: IntroStage(model: model)
                case .setup: SetupStage(model: model)
                case .firmware: FirmwareStage(model: model)
                case .claim: ClaimStage(model: model)
                case .onboarding: OnboardStage(model: model)
                case .ready: MainTabs(model: model)
                }
            }
            if splash {
                Splash()
                    .transition(.opacity)
            }
        }
        .font(Theme.body)
        .foregroundStyle(Theme.ink)
        .tint(Theme.ink)
        .background(Theme.paper.ignoresSafeArea())
        // Black on white, always: dark mode would invert the whole idea.
        .preferredColorScheme(.light)
        .task {
            try? await Task.sleep(for: .seconds(1.8))
            withAnimation(.easeInOut(duration: 0.45)) { splash = false }
        }
    }
}

/// The camera turning once on an empty page, which is the whole launch screen.
struct Splash: View {
    @State private var turn = 0

    var body: some View {
        VStack(spacing: 0) {
            Spacer()
            Device(turn: turn)
            Wordmark()
                .padding(.top, 8)
            Spacer()
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Ground())
        .onAppear { turn = 1 }
    }
}

/// The logo itself, the same file the board is silkscreened from.
struct Wordmark: View {
    var width: CGFloat = 190

    var body: some View {
        Image("Wordmark")
            .renderingMode(.template)
            .resizable()
            .aspectRatio(contentMode: .fit)
            .frame(width: width)
            .accessibilityLabel("Little Camera")
    }
}

/// A stage that leads with the camera itself, floating, the way the landing page shows it.
/// The camera and what it says sit together in the middle; what it asks for sits at the bottom.
struct DeviceStage<Content: View, Actions: View>: View {
    let title: String
    var mood: DeviceMood = .asleep
    var turn: Int = 0
    var lit: Bool = true
    /// A way back out of a step, which belongs in the corner rather than under the primary action.
    var back: (() -> Void)?
    @ViewBuilder var content: () -> Content
    @ViewBuilder var actions: () -> Actions

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                if let back {
                    Button(action: back) {
                        Image(systemName: "chevron.left")
                            .font(.system(size: 20, weight: .semibold))
                            .accessibilityLabel("Back")
                    }
                }
                Spacer(minLength: 0)
            }
            .frame(height: 30)
            .padding(.horizontal, Theme.pad)
            Spacer(minLength: 24)
            Device(mood: mood, turn: turn, lit: lit)
            VStack(spacing: 12) {
                Text(title)
                    .font(Theme.font(30, bold: true))
                    .kerning(-1)
                    .fixedSize(horizontal: false, vertical: true)
                content()
            }
            .font(Theme.lead)
            .multilineTextAlignment(.center)
            .frame(maxWidth: .infinity)
            .padding(.horizontal, Theme.pad)
            .padding(.top, 28)
            Spacer(minLength: 24)
            VStack(spacing: 10) { actions() }
                .card()
        }
        .background(Ground())
    }
}
