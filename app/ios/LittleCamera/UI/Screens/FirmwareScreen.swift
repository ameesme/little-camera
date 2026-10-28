import SwiftUI

/// What the camera runs against what the server published. The camera draws its own bar; this one is for whoever has to stand still.
struct FirmwareSection: View {
    let model: AppModel

    private var firmware: FirmwareUpdate { model.firmware }

    var body: some View {
        Section {
            if firmware.isRunning {
                ProgressRule(fraction: firmware.fraction)
            }
            if let notes = firmware.release?.notes, !notes.isEmpty {
                Text(notes).font(Theme.small)
            }
            if firmware.isAvailable(for: model.camera), let release = firmware.release {
                Text("Keep the app open and little camera close. A failure costs only time.")
                    .font(Theme.small)
                StatRow(label: "Image", value: "\(release.size / 1024) KB")
                Button {
                    firmware.send(to: model.camera)
                } label: {
                    Caption("Update to \(release.version)")
                }
                .buttonStyle(BlackButton())
            }
        } header: {
            Caption(firmware.line(installed: model.camera.info?.firmware))
        }
    }
}
