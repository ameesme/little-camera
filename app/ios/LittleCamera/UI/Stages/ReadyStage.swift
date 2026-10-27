import SwiftUI

/// The running app: one screen at a time, picked from the bar at the bottom.
struct ReadyStage: View {
    let model: AppModel
    @State private var screen: Screen = .camera

    enum Screen: String, CaseIterable, Identifiable {
        case camera
        case firmware
        case people
        case log

        var id: Self { self }

        var title: String {
            switch self {
            case .camera: return "Camera"
            case .firmware: return "Firmware"
            case .people: return "People"
            case .log: return "Log"
            }
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            StageHeader(title: screen.title, code: model.camera.shortCode)
            ScrollView {
                switch screen {
                case .camera: CameraScreen(model: model)
                case .firmware: FirmwareScreen(model: model)
                case .people: PeopleScreen(blog: model.blog)
                case .log: LogScreen(log: model.log)
                }
            }
            Rule()
            ScreenBar(selection: $screen, badge: model.blog.waiting.count)
        }
    }
}

/// Four captions and a 1 px rule. The selected one is inverted, which is the app's only way of saying "here".
struct ScreenBar: View {
    @Binding var selection: ReadyStage.Screen
    let badge: Int

    var body: some View {
        HStack(spacing: 0) {
            ForEach(ReadyStage.Screen.allCases) { screen in
                Button {
                    selection = screen
                } label: {
                    Caption(label(for: screen))
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 12)
                        .foregroundStyle(screen == selection ? Theme.paper : Theme.ink)
                        .background(screen == selection ? Theme.ink : Theme.paper)
                }
                .buttonStyle(.plain)
            }
        }
    }

    private func label(for screen: ReadyStage.Screen) -> String {
        screen == .people && badge > 0 ? "\(screen.title) \(badge)" : screen.title
    }
}
