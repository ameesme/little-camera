import SwiftUI

/// Nothing to decide: a camera with charge in it updates itself while the phone is next to it.
/// A flat one is asked about, because losing the radio halfway is the one way this goes wrong.
struct FirmwareStage: View {
    let model: AppModel

    private var charged: Bool { model.firmware.isChargedEnough(model.camera) }
    private var failed: String? { model.firmware.failure }

    var body: some View {
        DeviceStage(title: title, mood: mood) {
            if let failed {
                Text("Something went wrong while freshing up")
                Pill(failed).padding(.top, 10)
            } else if charged {
                Text("This will only take a minute or so.")
                ProgressRule(fraction: model.firmware.fraction)
                    .padding(.top, 4)
                    .task(id: charged) {
                        if !model.firmware.isRunning { model.firmware.send(to: model.camera) }
                    }
            } else {
                Text("Charge little camera for a few minutes to continue.")
            }
        } actions: {
            if failed != nil {
                Button { model.firmware.send(to: model.camera) } label: { Caption("Retry") }
                    .buttonStyle(BlackButton())
                Button { model.deferFirmware() } label: { Caption("Ignore") }
                    .buttonStyle(QuietButton())
            } else if !charged {
                Button { model.refresh() } label: { Caption("Retry") }
                    .buttonStyle(BlackButton())
                Button { model.deferFirmware() } label: { Caption("Skip it") }
                    .buttonStyle(QuietButton())
            } else if !model.firmware.isRunning {
                Button { model.deferFirmware() } label: { Caption("Not now") }
                    .buttonStyle(QuietButton())
            }
        }
    }

    private var title: String {
        if failed != nil { return "That didn’t work" }
        return charged ? "Freshing up your little camera" : "Charge it first"
    }

    /// A flat battery is about the battery; everything else is about the update.
    private var mood: DeviceMood {
        if failed != nil { return .alert }
        return charged ? .sparkles : .battery
    }
}
