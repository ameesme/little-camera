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
            } else if let release = firmware.release, firmware.isBehind(installed: installed) {
                Text("Update \(release.version) is ready to install").font(Theme.body)
            } else {
                Text("Up to date").font(Theme.body)
            }
            if !firmware.isRunning, firmware.isBehind(installed: installed) {
                Text("It will install automatically when little camera connects.")
                    .font(Theme.lead)
            }
            if let notes = firmware.release?.notes, !notes.isEmpty, firmware.isBehind(installed: installed) {
                Text(notes).font(Theme.small)
            }
        } header: {
            Caption("Firmware")
        }
    }
}
