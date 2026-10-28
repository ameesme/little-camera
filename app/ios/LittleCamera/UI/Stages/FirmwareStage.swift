import SwiftUI

/// Nothing to decide: a camera with charge in it updates itself while the phone is next to it.
/// A flat one is asked about, because losing the radio halfway is the one way this goes wrong.
struct FirmwareStage: View {
    let model: AppModel

    private var charged: Bool { model.firmware.isChargedEnough(model.camera) }

    var body: some View {
        DeviceStage(title: charged ? "Time to freshen up" : "Charge it first", mood: .send) {
            if charged {
                Text("Your little camera is getting up to date.")
                ProgressRule(fraction: model.firmware.fraction)
                Waiting(text: model.firmware.line(installed: model.camera.info?.firmware))
            } else {
                Text("Plug it in and this happens on its own.")
                Caption("\(model.camera.info?.batteryPercent ?? 0) % LEFT")
            }
        } actions: {
            Button { model.deferFirmware() } label: { Caption(charged ? "Not now" : "Skip it") }
                .buttonStyle(QuietButton())
        }
        .task(id: charged) {
            if charged { model.firmware.send(to: model.camera) }
        }
    }
}
