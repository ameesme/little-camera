import SwiftUI

/// Nothing to decide: a camera with charge in it updates itself while the phone is next to it.
/// A flat one is asked about, because losing the radio halfway is the one way this goes wrong.
struct FirmwareStage: View {
    let model: AppModel

    private var charged: Bool { model.firmware.isChargedEnough(model.camera) }

    var body: some View {
        DeviceStage(title: charged ? "Hold on a minute" : "Charge it first", mood: .sparkles) {
            if charged {
                Text("Your little camera is getting up to date.")
                ProgressRule(fraction: model.firmware.fraction)
                    .padding(.top, 4)
                Text(model.firmware.line(installed: model.camera.info?.firmware))
                    .font(Theme.body)
            } else {
                Text("There is \(model.camera.info?.batteryPercent ?? 0) % left. Plug it in and this happens on its own.")
            }
        } actions: {
            if !model.firmware.isRunning {
                Button { model.deferFirmware() } label: { Caption(charged ? "Not now" : "Skip it") }
                    .buttonStyle(QuietButton())
            }
        }
    }
}
