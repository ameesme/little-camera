import SwiftUI

/// One stage at a time. Setup and linking own the whole screen; after that the screens take over.
struct RootView: View {
    let model: AppModel

    var body: some View {
        Group {
            switch model.stage {
            case .setup: SetupStage(model: model)
            case .linking: LinkStage(model: model)
            case .ready: ReadyStage(model: model)
            }
        }
        .font(Theme.body)
        .foregroundStyle(Theme.ink)
        .tint(Theme.ink)
        .background(Theme.paper.ignoresSafeArea())
        // Black on white, always: dark mode would invert the whole idea.
        .preferredColorScheme(.light)
    }
}

/// The bar every stage wears: what the app is, and the camera's short code for linking on the web.
struct StageHeader: View {
    let title: String
    let code: String?

    var body: some View {
        VStack(spacing: 0) {
            HStack(alignment: .firstTextBaseline) {
                Caption(title)
                Spacer()
                if let code {
                    Text(code)
                        .font(Theme.font(14, bold: true))
                        .kerning(2)
                }
            }
            .card()
            Rule()
        }
    }
}
