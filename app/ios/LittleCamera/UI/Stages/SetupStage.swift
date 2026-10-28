import SwiftUI

/// First launch: find the camera. One camera is no choice at all, so it connects on its own;
/// a room with two of them is the only time this asks.
struct SetupStage: View {
    let model: AppModel

    private var found: [CameraSession.Found] { model.camera.found }

    var body: some View {
        DeviceStage(
            title: found.isEmpty ? "Wake little camera" : "Hold still",
            mood: .viewfinder,
            turn: found.isEmpty ? 0 : 1,
            lit: !found.isEmpty
        ) {
            if found.isEmpty {
                Text("Press the button to wake it up")
            }
            if model.camera.bluetooth != .poweredOn {
                Caption(model.camera.line)
            } else {
                Waiting(text: found.isEmpty ? "Listening for it" : "Connecting")
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
}
