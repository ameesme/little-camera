import SwiftUI
import UIKit

/// First launch: find little camera. One camera is no choice at all, so it connects on its own.
/// Nothing is paired until the owner types the number off its panel, which is the one thing
/// only someone holding it can do.
struct SetupStage: View {
    let model: AppModel

    private var found: [CameraSession.Found] { model.camera.found }
    private var pairing: Bool { model.camera.state == .pairing }
    /// The camera has answered: it is this phone's now, and the way in is done.
    private var hello: Bool {
        Demo.stage == nil ? model.camera.isLinked && model.camera.info != nil : Demo.hello
    }
    private var radioReady: Bool { model.camera.bluetooth == .poweredOn }
    var body: some View {
        DeviceStage(title: title,
                    mood: hello ? .ohhi : pairing ? .passkey : .asleep,
                    turn: found.isEmpty ? 0 : 1,
                    lit: hello || !found.isEmpty,
                    back: hello ? nil : { model.startOver() }) {
            if hello {
                Text("It is now ready to take pictures.\nNext: a little blog to put them on.")
            } else if pairing {
                Text("It is showing six numbers. Type them in to let this phone in.")
            } else if found.isEmpty {
                Text("Hold the button to wake it up.")
            }
            if !radioReady {
                Pill(model.camera.line)
                    .padding(.top, 10)
            } else if let error = model.camera.error {
                Pill(error)
                    .padding(.top, 10)
            } else if !pairing && !hello {
                Waiting(text: found.isEmpty ? "Looking for your little camera" : "Connecting")
            }
        } actions: {
            if hello {
                Button { model.confirmConnected() } label: { Caption("Continue") }
                    .buttonStyle(BlackButton())
            }
            if model.camera.declined {
                Button { model.camera.retryPairing() } label: { Caption("Try again") }
                    .buttonStyle(BlackButton())
            }
            // Nothing here can be fixed from inside the app, so the only useful button is the way out to where it can.
            if !radioReady, let settings = URL(string: UIApplication.openSettingsURLString) {
                Link(destination: settings) { Caption("Open settings") }
                    .buttonStyle(BlackButton())
            }
            ForEach(found.count > 1 ? found : []) { camera in
                Button { model.useCamera(camera) } label: { Caption(camera.name) }
                    .buttonStyle(BlackButton())
            }
        }
        .task(id: hello) {
            if hello { model.camera.sayHello() }
        }
        .task(id: found.count) {
            guard !hello, found.count == 1, let only = found.first else { return }
            // Connecting is instant, and the owner never sees it happen without a beat to read.
            try? await Task.sleep(for: .seconds(2))
            model.useCamera(only)
        }
    }

    private var title: String {
        if hello { return "Oh hi little camera!" }
        if found.isEmpty { return "Wake little camera" }
        return pairing ? "Pair little camera" : "There it is!"
    }
}
