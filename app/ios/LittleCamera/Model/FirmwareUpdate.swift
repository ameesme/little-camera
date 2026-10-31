import Foundation

/// The firmware the camera runs against the firmware the server has published, and the push between them (§3.7).
@Observable @MainActor
final class FirmwareUpdate {
    private(set) var release: FirmwareRelease?
    private(set) var phase: FirmwareUpdater.Phase = .idle

    private let server: ServerClient
    private let log: SyncLog
    @ObservationIgnored private var updater: FirmwareUpdater?
    @ObservationIgnored private var task: Task<Void, Never>?

    init(server: ServerClient, log: SyncLog) {
        self.server = server
        self.log = log
    }

    func seedDemo(release: FirmwareRelease) {
        self.release = release
        if let phase = Demo.phase { self.phase = phase }
    }

    func attach(link: CameraLink) {
        let updater = FirmwareUpdater(link: link, server: server, log: log)
        updater.onPhase = { [weak self] phase in self?.phase = phase }
        self.updater = updater
    }

    var isRunning: Bool { phase.isRunning }

    var fraction: Double {
        switch phase {
        case .sending(let sent, let total) where total > 0: return Double(sent) / Double(total)
        case .verifying, .restarting: return 1
        default: return 0
        }
    }

    /// What the push is doing right now, for whoever has to stand still through it.
    var line: String {
        switch phase {
        case .downloading: return "Fetching the update"
        case .sending(let sent, let total): return "\(sent * 100 / max(total, 1)) %"
        case .verifying: return "Little camera is checking it"
        case .restarting: return "Little camera is restarting"
        case .failed, .idle: return ""
        }
    }

    /// Why the last push stopped, or nil. Its own property because a failure is a screen, not a line.
    var failure: String? {
        if case .failed(let why) = phase { return why }
        return nil
    }

    /// The two states the camera tab reports: either it is current or there is something to press.
    func isBehind(installed: String?) -> Bool {
        guard let installed, let release else { return false }
        return release.version != installed
    }

    /// Flash is written from the camera's main loop and a brownout halfway leaves it on the old
    /// image, which is survivable but wastes the whole transfer. A third of a battery is more than
    /// the push needs; below that it is worth waiting for a cable rather than asking twice.
    static let batteryFloor: UInt8 = 33

    func isChargedEnough(_ camera: CameraSession) -> Bool {
        guard let info = camera.info else { return false }
        return info.charging || info.onUsb || info.batteryPercent >= FirmwareUpdate.batteryFloor
    }

    /// Worth offering only when the camera can take it: new enough to say what it runs, with room in its spare slot.
    func isAvailable(for camera: CameraSession) -> Bool {
        guard let updater, !updater.isRunning, let release else { return false }
        guard let info = camera.info, info.canTake(imageOf: release.size) else { return false }
        guard camera.isReady, camera.canUpdateFirmware, !camera.isSyncing else { return false }
        return release.version != info.firmware
    }

    /// Against the version the camera itself reported, not the one the server last heard about.
    func refresh(installed: String?, published: FirmwareRelease?) {
        if let published { release = published }
        Task { @MainActor in
            do {
                release = try await server.latestFirmware(installed: installed)
            } catch {
                // A camera that cannot be updated is still a camera; the line keeps saying what it knows.
                log.add("firmware check failed: \(error.localizedDescription)")
            }
        }
    }

    func send(to camera: CameraSession) {
        guard let updater, let release, let info = camera.info, isAvailable(for: camera) else { return }
        task?.cancel()
        task = Task { @MainActor in
            await updater.run(release: release, info: info)
        }
    }

    /// The camera is back, so whichever way the last push ended, the card goes back to reporting what it runs now.
    func settle() {
        updater?.settle()
    }
}
