import SwiftUI

/// What the camera runs against what the server published. The camera draws its own bar; this one is for whoever has to stand still.
struct FirmwareSection: View {
    let model: AppModel

    private var firmware: FirmwareUpdate { model.firmware }
    private var installed: String? { model.camera.info?.firmware }

    var body: some View {
        Section {
            if firmware.isRunning {
                Text(firmware.line).font(Theme.body)
                ProgressRule(fraction: firmware.fraction)
            } else if let failed = firmware.failure {
                Text(failed).font(Theme.small)
            } else {
                // Two states, and only one of them is worth a button: a version pair said
                // nothing anyone could act on.
                Text(firmware.isBehind(installed: installed) ? "Update available" : "Up to date")
                    .font(Theme.body)
            }
            if let notes = firmware.release?.notes, !notes.isEmpty, firmware.isBehind(installed: installed) {
                Text(notes).font(Theme.small)
            }
            if firmware.isAvailable(for: model.camera) {
                Text("Keep the app open and little camera close. A failure costs only time.")
                    .font(Theme.small)
                Button {
                    firmware.send(to: model.camera)
                } label: {
                    Caption("Update little camera")
                }
                .buttonStyle(BlackButton())
            }
        } header: {
            Caption("Firmware")
        }
    }
}
