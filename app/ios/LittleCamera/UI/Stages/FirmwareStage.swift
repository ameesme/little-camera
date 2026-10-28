import SwiftUI

/// Offered once, right after pairing. Skippable: a camera that works is more use than one that is current.
struct FirmwareStage: View {
    let model: AppModel

    var body: some View {
        DeviceStage(title: "New firmware", mood: .send) {
            if let release = model.firmware.release {
                Caption("\(model.camera.info?.firmware ?? "?") → \(release.version)")
                if let notes = release.notes, !notes.isEmpty {
                    Text(notes)
                }
                Text("Takes a minute. Keep the phone close to it.")
                    .font(Theme.small)
            }
            if model.firmware.isRunning {
                ProgressRule(fraction: model.firmware.fraction)
                Caption(model.firmware.line(installed: model.camera.info?.firmware))
            }
        } actions: {
            if !model.firmware.isRunning {
                Button { model.firmware.send(to: model.camera) } label: { Caption("Install it") }
                    .buttonStyle(BlackButton())
                Button { model.deferFirmware() } label: { Caption("Later") }
                    .buttonStyle(QuietButton())
            }
        }
    }
}
