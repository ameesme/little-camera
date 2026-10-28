import SwiftUI

/// First launch: find little camera. One camera is no choice at all, so it connects on its own.
/// Nothing is paired until the owner types the number off its panel, which is the one thing
/// only someone holding it can do.
struct SetupStage: View {
    let model: AppModel

    private var found: [CameraSession.Found] { model.camera.found }
    private var pairing: Bool { model.camera.state == .pairing }

    var body: some View {
        DeviceStage(title: title, mood: .viewfinder, turn: found.isEmpty ? 0 : 1, lit: !found.isEmpty) {
            if found.isEmpty {
                Text("Press the button to wake it up.")
            } else if pairing {
                Text("It is showing six numbers. Type them in to let this phone in.")
            }
            if model.camera.bluetooth != .poweredOn {
                Text(model.camera.line)
            } else if !pairing {
                Waiting(text: found.isEmpty ? "Looking for your little camera" : "Connecting")
            }
        } actions: {
            ForEach(found.count > 1 ? found : []) { camera in
                Button { model.useCamera(camera) } label: { Caption(camera.name) }
                    .buttonStyle(BlackButton())
            }
        }
        .task(id: found.count) {
            if found.count == 1, let only = found.first { model.useCamera(only) }
        }
    }

    private var title: String {
        if found.isEmpty { return "Wake little camera" }
        return pairing ? "Check its screen" : "There it is!"
    }
}
