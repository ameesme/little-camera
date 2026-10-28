import SwiftUI

/// What the camera runs against what the server published. The camera draws its own bar; this one is for whoever has to stand still.
struct FirmwareScreen: View {
    let model: AppModel

    private var firmware: FirmwareUpdate { model.firmware }

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            VStack(alignment: .leading, spacing: 10) {
                Caption(firmware.line(installed: model.camera.info?.firmware))
                if firmware.isRunning {
                    ProgressRule(fraction: firmware.fraction)
                }
                if let notes = firmware.release?.notes, !notes.isEmpty {
                    Text(notes).font(Theme.small)
                }
            }
            .card()
            Rule()
            if firmware.isAvailable(for: model.camera), let release = firmware.release {
                VStack(alignment: .leading, spacing: 10) {
                    Text("Keep the app open and the camera close. A failure costs only time.")
                        .font(Theme.small)
                    StatRow(label: "Image", value: "\(release.size / 1024) KB")
                    Button {
                        firmware.send(to: model.camera)
                    } label: {
                        Caption("Update to \(release.version)")
                    }
                    .buttonStyle(BlackButton())
                }
                .card()
                Rule()
            }
        }
    }
}
