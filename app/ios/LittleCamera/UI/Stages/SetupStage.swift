import SwiftUI

/// First launch: which camera is yours. Pairing itself happens on the first connection.
struct SetupStage: View {
    let model: AppModel

    private var found: [CameraSession.Found] { model.camera.found }

    var body: some View {
        DeviceStage(
            title: found.isEmpty ? "Wake your camera" : "There it is",
            mood: .viewfinder,
            turn: found.isEmpty ? 0 : 1,
            lit: !found.isEmpty
        ) {
            Text(found.isEmpty ? "Press the button to wake it up" : "Ready when you are.")
            if model.camera.bluetooth != .poweredOn {
                Caption(model.camera.line)
            } else if found.isEmpty {
                Waiting(text: "Listening for it")
            }
        } actions: {
            ForEach(found) { camera in
                Button { model.useCamera(camera) } label: {
                    Caption(found.count > 1 ? camera.name : "Connect")
                }
                .buttonStyle(BlackButton())
            }
        }
    }
}
