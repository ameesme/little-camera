import SwiftUI

/// How little camera is doing, as it reports it. Nothing on this screen is computed here.
/// The camera and the one button that acts on it stay put; its numbers scroll under them.
struct CameraTab: View {
    let model: AppModel
    @State private var confirmingDisconnect = false
    @State private var confirmingResync = false

    private var camera: CameraSession { model.camera }
    private var info: CameraInfo? { camera.info }

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Sending, not browsing: the gallery scene is a stock photo and the camera
                // is not showing anyone a stock photo while it hands pictures over.
                Device(mood: camera.isSyncing ? .send : DeviceMood(info?.face ?? .content))
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

                // Nothing to ask for while it is already happening.
                if !camera.isSyncing {
                    Button { camera.syncNow() } label: { Caption("Sync now") }
                        .buttonStyle(BlackButton())
                        .card()
                }

                numbers
            }
            .background(Ground())
            .navigationBarTitleDisplayMode(.inline)
            .confirmationDialog("Disconnect little camera?", isPresented: $confirmingDisconnect, titleVisibility: .visible) {
                Button("Disconnect and log out", role: .destructive) { model.disconnect() }
            } message: {
                Text("You will have to photograph the code again to come back.")
            }
            .confirmationDialog("Send every picture again?", isPresented: $confirmingResync, titleVisibility: .visible) {
                Button("Send everything") { camera.resyncEverything() }
            } message: {
                Text("Everything still on the camera goes to this blog, even what it has already sent.")
            }
        }
    }

    private var numbers: some View {
        List {
            if let info {
                Section {
                    // Both numbers on one line, because the percentage comes off a curve nobody
                    // has calibrated yet and the millivolts are the measurement (firmware/AGENTS.md).
                    Gauge(value: "\(info.batteryPercent) %", aside: voltage(info), fraction: Double(info.batteryPercent) / 100)
                } header: {
                    Caption("Battery")
                }

                Section {
                    if info.totalBytes > 0 {
                        Gauge(value: "\(info.photoCount) photos",
                              aside: "\(bytes(info.usedBytes)) of \(bytes(info.totalBytes))",
                              fraction: info.usedFraction)
                    } else {
                        Gauge(value: "\(info.photoCount) photos", aside: "Not mounted", fraction: 0)
                    }
                    if info.unsyncedCount > 0 {
                        Text("\(info.unsyncedCount) still to come over").font(Theme.small)
                    }
                } header: {
                    HStack {
                        Caption("Memory")
                        Spacer()
                        if !camera.isSyncing, info.photoCount > 0 {
                            Button { confirmingResync = true } label: {
                                Image(systemName: "arrow.counterclockwise")
                                    .font(.system(size: 12, weight: .bold))
                            }
                            .foregroundStyle(Theme.ink)
                        }
                    }
                }
            }

            FirmwareSection(model: model)

            Section {
                Text("Disconnect this little camera and log out.")
                    .font(Theme.body)
                Button(role: .destructive) { confirmingDisconnect = true } label: {
                    Caption("Disconnect and log out")
                }
                .buttonStyle(BlackButton())
            } header: {
                Caption("Goodbye")
            }

            if let error = camera.error {
                Pill(error).frame(maxWidth: .infinity)
            }
        }
        .listStyle(.plain)
        .scrollContentBackground(.hidden)
        .environment(\.defaultMinListRowHeight, 0)
        .defaultScrollAnchor(.top)
        .defaultScrollAnchor(.top, for: .sizeChanges)
        .listSectionSeparator(.hidden)
        .listRowSeparatorTint(Theme.ink)
        .listRowBackground(Color.clear)
        .listSectionSpacing(0)
    }

    private var lastSeen: String {
        if camera.isReady { return "Here now" }
        guard let last = camera.lastSyncAt else { return "Not seen yet" }
        let since = RelativeDateTimeFormatter()
        since.unitsStyle = .full
        return "Last seen \(since.localizedString(for: last, relativeTo: .now))"
    }

    /// Charging says more about the number than the number does, so it takes the slot.
    private func voltage(_ info: CameraInfo) -> String {
        if info.charging { return "Charging" }
        if info.onUsb { return "USB, charged" }
        return String(format: "%.2f V", Double(info.batteryMv) / 1000)
    }

    private func bytes(_ value: UInt32) -> String {
        let kb = Double(value) / 1024
        return kb < 1024 ? String(format: "%.0f KB", kb) : String(format: "%.1f MB", kb / 1024)
    }
}
