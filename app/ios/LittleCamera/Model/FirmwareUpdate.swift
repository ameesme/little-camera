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

    func line(installed: String?) -> String {
        switch phase {
        case .downloading: return "Fetching the update"
        case .sending(let sent, let total): return "Sending, \(sent * 100 / max(total, 1)) %"
        case .verifying: return "Little camera is checking it"
        case .restarting: return "Little camera is restarting"
        case .failed(let why): return "It did not take: \(why)"
        case .idle:
            guard let installed else { return "Firmware, version unknown" }
            guard let release, release.version != installed else { return "Firmware \(installed), up to date" }
            return "Firmware \(installed), \(release.version) is out"
        }
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
