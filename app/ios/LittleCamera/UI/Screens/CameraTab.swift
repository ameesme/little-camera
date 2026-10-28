import SwiftUI

/// How the camera is doing, as the camera reports it. Nothing on this screen is computed here.
struct CameraTab: View {
    let model: AppModel
    @State private var confirmingDisconnect = false

    private var camera: CameraSession { model.camera }
    private var info: CameraInfo? { camera.info }

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(alignment: .leading, spacing: 0) {
                    Device(mood: camera.isSyncing ? .gallery : DeviceMood(info?.face ?? .content))
                        .padding(.top, 8)
                        .frame(maxWidth: .infinity)

                    VStack(alignment: .leading, spacing: 10) {
                        Caption(camera.line)
                        if camera.isSyncing {
                            ProgressRule(fraction: camera.progress)
                        }
                        Button { camera.syncNow() } label: { Caption("Sync now") }
                            .buttonStyle(BlackButton())
                    }
                    .card()
                    Rule()

                    if let info {
                        VStack(alignment: .leading, spacing: 10) {
                            Caption("Battery")
                            // Both numbers, because the percentage comes off a curve nobody has calibrated
                            // yet and the millivolts are the measurement (firmware/AGENTS.md).
                            StatRow(label: "Charge", value: "\(info.batteryPercent) %")
                            StatRow(label: "Voltage", value: String(format: "%.2f V", Double(info.batteryMv) / 1000))
                            if info.charging {
                                StatRow(label: "Power", value: "Charging")
                            } else if info.onUsb {
                                StatRow(label: "Power", value: "USB, charged")
                            }
                            ProgressRule(fraction: Double(info.batteryPercent) / 100)
                        }
                        .card()
                        Rule()

                        VStack(alignment: .leading, spacing: 10) {
                            Caption("Memory")
                            StatRow(label: "Photos", value: "\(info.photoCount)")
                            StatRow(label: "Not sent yet", value: "\(info.unsyncedCount)")
                            if info.totalBytes > 0 {
                                StatRow(label: "Used", value: "\(bytes(info.usedBytes)) of \(bytes(info.totalBytes))")
                                ProgressRule(fraction: info.usedFraction)
                            } else {
                                StatRow(label: "Storage", value: "Not mounted")
                            }
                        }
                        .card()
                        Rule()
                    }

                    FirmwareScreen(model: model)
                    Rule()

                    VStack(alignment: .leading, spacing: 10) {
                        Caption("This phone")
                        Text("Forgets little camera and the little blog on this phone. Both stay where they are.")
                            .font(Theme.small)
                        Button(role: .destructive) { confirmingDisconnect = true } label: {
                            Caption("Disconnect and log out")
                        }
                        .buttonStyle(BlackButton())
                    }
                    .card()

                    if let error = camera.error {
                        Text(error).font(Theme.small).card()
                    }
                }
            }
            .refreshable { model.refresh() }
            .navigationTitle("Camera")
            .navigationBarTitleDisplayMode(.inline)
            .confirmationDialog("Disconnect little camera?", isPresented: $confirmingDisconnect, titleVisibility: .visible) {
                Button("Disconnect and log out", role: .destructive) { model.disconnect() }
            } message: {
                Text("You will have to photograph the code again to come back.")
            }
        }
    }

    private func bytes(_ value: UInt32) -> String {
        let kb = Double(value) / 1024
        return kb < 1024 ? String(format: "%.0f KB", kb) : String(format: "%.1f MB", kb / 1024)
    }
}
