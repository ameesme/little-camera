import SwiftUI

/// How little camera is doing, as it reports it. Nothing on this screen is computed here.
/// The camera and the one button that acts on it stay put; its numbers scroll under them.
struct CameraTab: View {
    let model: AppModel
    @State private var confirmingDisconnect = false

    private var camera: CameraSession { model.camera }
    private var info: CameraInfo? { camera.info }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                Device(mood: camera.isSyncing ? .gallery : DeviceMood(info?.face ?? .content))
                    .padding(.top, 8)

                Text(lastSeen)
                    .font(Theme.body)
                    .multilineTextAlignment(.center)
                    .frame(maxWidth: .infinity)
                    .padding(.top, 14)

                if camera.isSyncing {
                    ProgressRule(fraction: camera.progress)
                        .padding(.horizontal, Theme.pad)
                        .padding(.top, 10)
                }

                Button { camera.syncNow() } label: { Caption("Sync now") }
                    .buttonStyle(BlackButton())
                    .card()

                numbers
            }
            .background(Ground())
            .navigationBarTitleDisplayMode(.inline)
            .confirmationDialog("Disconnect little camera?", isPresented: $confirmingDisconnect, titleVisibility: .visible) {
                Button("Disconnect and log out", role: .destructive) { model.disconnect() }
            } message: {
                Text("You will have to photograph the code again to come back.")
            }
        }
    }

    private var numbers: some View {
        List {
            if let info {
                Section {
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
                } header: {
                    Caption("Battery")
                }

                Section {
                    StatRow(label: "Photos", value: "\(info.photoCount)")
                    StatRow(label: "Not sent yet", value: "\(info.unsyncedCount)")
                    if info.totalBytes > 0 {
                        StatRow(label: "Used", value: "\(bytes(info.usedBytes)) of \(bytes(info.totalBytes))")
                        ProgressRule(fraction: info.usedFraction)
                    } else {
                        StatRow(label: "Storage", value: "Not mounted")
                    }
                } header: {
                    Caption("Memory")
                }
            }

            FirmwareSection(model: model)

            Section {
                Text("Forgets little camera and the little blog on this phone. Both stay where they are.")
                    .font(Theme.small)
                Button(role: .destructive) { confirmingDisconnect = true } label: {
                    Caption("Disconnect and log out")
                }
                .buttonStyle(BlackButton())
            } header: {
                Caption("This phone")
            }

            if let error = camera.error {
                Text(error).font(Theme.small)
            }
        }
        .listStyle(.plain)
        .defaultScrollAnchor(.top)
        .defaultScrollAnchor(.top, for: .sizeChanges)
        .listRowSeparatorTint(Theme.ink)
    }

    private var lastSeen: String {
        if camera.isReady { return "Here now" }
        guard let last = camera.lastSyncAt else { return "Not seen yet" }
        let since = RelativeDateTimeFormatter()
        since.unitsStyle = .full
        return "Last seen \(since.localizedString(for: last, relativeTo: .now))"
    }

    private func bytes(_ value: UInt32) -> String {
        let kb = Double(value) / 1024
        return kb < 1024 ? String(format: "%.0f KB", kb) : String(format: "%.1f MB", kb / 1024)
    }
}
