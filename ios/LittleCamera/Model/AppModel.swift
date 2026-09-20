import Foundation
import CoreBluetooth
import CoreGraphics

/// Everything the UI shows, and the owner of the link, the sync engine and the
/// server client. Main actor only.
@Observable @MainActor
final class AppModel {
    enum LinkState: Equatable {
        case off
        case scanning
        case connecting
        case pairing
        case connected(String)
        case syncing(Int, Int)
    }

    struct FoundCamera: Identifiable, Equatable {
        let id: UUID
        let name: String
    }

    // Link
    private(set) var linkState: LinkState = .off
    private(set) var bluetoothState: CBManagerState = .unknown
    private(set) var isSetUp: Bool
    private(set) var found: [FoundCamera] = []
    private(set) var info: CameraInfo?
    private(set) var cameraName: String?
    private(set) var cameraId: String?

    // Sync
    private(set) var lastSyncAt: Date?
    private(set) var uploadedCount: Int
    private(set) var recentThumbnails: [CGImage] = []
    private(set) var lastError: String?

    // Server
    private(set) var serverBaseURL: String
    private(set) var status: StatusResponse?
    private(set) var serverError: String?
    private(set) var avatarPending = false

    // Firmware (protocol §3.7, §4.7)
    private(set) var release: FirmwareRelease?
    private(set) var updatePhase: FirmwareUpdater.Phase = .idle

    let log = SyncLog()

    // `let`s are never observed; the vars below are plumbing, not UI state.
    private let settings = Settings()
    private let server: ServerClient
    @ObservationIgnored private var link: CameraLink?
    @ObservationIgnored private var sync: SyncEngine?
    @ObservationIgnored private var updater: FirmwareUpdater?
    @ObservationIgnored private var sessionTask: Task<Void, Never>?
    @ObservationIgnored private var updateTask: Task<Void, Never>?

    init() {
        isSetUp = settings.pairedPeripheralId != nil
        cameraId = settings.cameraId
        lastSyncAt = settings.lastSyncAt
        uploadedCount = settings.uploadedCount
        serverBaseURL = settings.serverBaseURL
        server = ServerClient(baseURL: AppModel.url(from: settings.serverBaseURL))
        if let id = settings.cameraId, let secret = Keychain.secret(for: id) {
            server.credentials = ServerClient.Credentials(cameraId: id, secretHex: secret.hexString)
        }
    }

    var shortCode: String? {
        cameraId.map { ShortCode.compute($0) }
    }

    /// The link state in plain words, as the camera card shows it.
    var linkLine: String {
        switch linkState {
        case .off:
            switch bluetoothState {
            case .poweredOff: return "BLUETOOTH IS OFF"
            case .unauthorized: return "BLUETOOTH NOT ALLOWED FOR THIS APP"
            case .unsupported: return "NO BLUETOOTH ON THIS DEVICE"
            default: return "STARTING BLUETOOTH"
            }
        case .scanning, .connecting:
            return "LOOKING FOR CAMERA"
        case .pairing:
            return "PAIRING — TYPE THE CODE ON THE CAMERA"
        case .connected(let name):
            return "CONNECTED · \(name)"
        case .syncing(let done, let total):
            return "SYNCING \(done)/\(total)"
        }
    }

    /// What the firmware card says, in one line.
    var firmwareLine: String {
        switch updatePhase {
        case .downloading: return "DOWNLOADING NEW FIRMWARE"
        case .sending(let sent, let total):
            return "SENDING \(sent * 100 / max(total, 1))% — KEEP THE PHONE CLOSE"
        case .verifying: return "CAMERA IS CHECKING THE IMAGE"
        case .restarting: return "CAMERA IS RESTARTING"
        case .failed(let why): return "UPDATE FAILED — \(why.uppercased())"
        case .idle:
            guard let installed = info?.firmware else { return "FIRMWARE \u{2014} THIS CAMERA CANNOT SAY" }
            guard let release = release, release.version != installed else { return "FIRMWARE \(installed) \u{2014} UP TO DATE" }
            return "FIRMWARE \(installed) \u{2192} \(release.version) AVAILABLE"
        }
    }

    /// An update is worth offering only when the camera can actually take it:
    /// new enough to say what it runs, with room in its spare slot (§3.7).
    var canUpdateFirmware: Bool {
        // Offered again after a failure: the camera is untouched, so trying
        // once more is exactly as safe as the first time.
        guard let updater = updater, !updater.isRunning else { return false }
        guard let info = info, let release = release else { return false }
        guard let installed = info.firmware, info.canTake(imageOf: release.size) else { return false }
        guard let link = link, link.isReady, link.canUpdate else { return false }
        if sync?.isRunning == true { return false }
        return release.version != installed
    }

    var updateFraction: Double {
        if case .sending(let sent, let total) = updatePhase, total > 0 {
            return Double(sent) / Double(total)
        }
        if case .verifying = updatePhase { return 1 }
        if case .restarting = updatePhase { return 1 }
        return 0
    }

    var progressFraction: Double {
        if case .syncing(let done, let total) = linkState, total > 0 {
            return Double(done) / Double(total)
        }
        return lastSyncAt == nil ? 0 : 1
    }

    // MARK: Lifecycle

    func start() {
        guard link == nil else { return }
        let link = CameraLink(pairedId: settings.pairedPeripheralId) { [weak self] event in
            self?.handle(event)
        }
        self.link = link
        let sync = SyncEngine(link: link, server: server, settings: settings, log: log)
        sync.onEvent = { [weak self] event in
            self?.handle(event)
        }
        self.sync = sync
        let updater = FirmwareUpdater(link: link, server: server, log: log)
        updater.onPhase = { [weak self] phase in
            self?.updatePhase = phase
        }
        self.updater = updater
        link.start()
        if server.credentials != nil {
            refreshStatus()
        }
    }

    /// SetupView: the owner picked a camera. Remembers it and connects.
    func useCamera(_ camera: FoundCamera, serverURL: String) {
        setServerURL(serverURL)
        settings.pairedPeripheralId = camera.id
        cameraName = camera.name
        isSetUp = true
        log.add("using \(camera.name)")
        link?.use(peripheralId: camera.id)
    }

    func setServerURL(_ string: String) {
        let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        settings.serverBaseURL = trimmed
        serverBaseURL = trimmed
        server.baseURL = AppModel.url(from: trimmed)
    }

    // MARK: Actions

    func syncNow() {
        guard let link = link, link.isReady else {
            log.add("camera not connected; press its shutter to wake it")
            lastError = "Camera is asleep. Press its shutter and try again."
            return
        }
        beginSession()
    }

    func refreshStatus() {
        guard server.credentials != nil else { return }
        Task {
            do {
                let s = try await self.server.status()
                self.status = s
                self.serverError = nil
                // The server looked up the release while it was answering, so
                // take it from here rather than asking twice.
                if let firmware = s.firmware {
                    self.release = firmware.latest
                }
                // Pending = the server has a request and no avatar yet. A re-request
                // on top of an existing avatar is tracked by the local flag alone.
                if s.avatar.requestedAt == nil {
                    self.avatarPending = false
                } else if !s.avatar.hasAvatar {
                    self.avatarPending = true
                }
            } catch {
                self.serverError = error.localizedDescription
            }
        }
    }

    func addSubscriber(email: String, name: String?) {
        let trimmedEmail = email.trimmingCharacters(in: .whitespaces)
        let trimmedName = name?.trimmingCharacters(in: .whitespaces)
        let cleanName: String? = (trimmedName?.isEmpty ?? true) ? nil : trimmedName
        Task {
            do {
                _ = try await self.server.addSubscriber(email: trimmedEmail, name: cleanName)
                self.refreshStatus()
            } catch {
                self.serverError = error.localizedDescription
            }
        }
    }

    func approve(id: Int) {
        Task {
            do {
                try await self.server.approve(id: id)
                self.refreshStatus()
            } catch {
                self.serverError = error.localizedDescription
            }
        }
    }

    func block(id: Int) {
        Task {
            do {
                try await self.server.block(id: id)
                self.refreshStatus()
            } catch {
                self.serverError = error.localizedDescription
            }
        }
    }

    /// Fetch the newest release for whatever the camera says it is running.
    func refreshFirmware() {
        Task {
            do {
                self.release = try await self.server.latestFirmware(installed: self.info?.firmware)
            } catch {
                // Not worth a banner: a camera that cannot be updated is still
                // a camera, and the line just keeps saying what it knows.
                self.log.add("firmware check failed: \(error.localizedDescription)")
            }
        }
    }

    /// Push the available release to the camera. The camera reboots into it by
    /// itself; the link re-arms and the next session reads the new version.
    func updateFirmware() {
        guard canUpdateFirmware, let updater = updater, let release = release, let info = info else { return }
        updateTask?.cancel()
        updateTask = Task { [weak self] in
            let ok = await updater.run(release: release, info: info)
            guard let self = self, ok else { return }
            // Its old version is gone the moment it reboots; the next Info read
            // brings the new one, and the server hears about it at hello.
            self.info = nil
        }
    }

    func requestAvatar() {
        Task {
            do {
                _ = try await self.server.requestAvatar()
                self.avatarPending = true
                self.log.add("next picture becomes the profile picture")
            } catch {
                self.serverError = error.localizedDescription
            }
        }
    }

    // MARK: Link events

    private func handle(_ event: CameraLink.Event) {
        switch event {
        case .bluetooth(let state):
            bluetoothState = state
            if state != .poweredOn {
                linkState = .off
            }
        case .scanning:
            linkState = .scanning
        case .found(let id, let name):
            if !found.contains(where: { $0.id == id }) {
                found.append(FoundCamera(id: id, name: name))
            }
        case .connecting(let name):
            linkState = .connecting
            if cameraName == nil {
                cameraName = name
            }
        case .ready(let name):
            cameraName = name
            log.add("connected to \(name)")
            beginSession()
        case .disconnected(let reason):
            log.add("disconnected" + (reason.map { ": \($0)" } ?? ""))
            // The link re-arms its connect by itself; from here it looks like waiting.
            linkState = isSetUp ? .connecting : .scanning
        case .log(let line):
            log.add(line)
        }
    }

    private func handle(_ event: SyncEngine.Event) {
        switch event {
        case .progress(let done, let total):
            linkState = .syncing(done, total)
        case .uploaded(_, let image, let kind):
            uploadedCount = settings.uploadedCount
            if let image = image {
                recentThumbnails = Array((recentThumbnails + [image]).suffix(4))
            }
            if kind == "avatar" {
                avatarPending = false
            }
        case .finished(_, let info):
            self.info = info
            lastSyncAt = settings.lastSyncAt
            lastError = nil
            linkState = .connected(info.deviceName)
            refreshStatus()
        case .failed(let message):
            lastError = message
            if let link = link, link.isReady {
                linkState = .connected(cameraName ?? "camera")
            }
        }
    }

    // MARK: Session

    /// Runs once per connection (and on SYNC NOW): Info → Secret if needed → hello → sync.
    private func beginSession() {
        sessionTask?.cancel()
        sessionTask = Task { [weak self] in
            await self?.session()
        }
    }

    private func session() async {
        guard let link = link, let sync = sync else { return }
        do {
            let info = try await link.readInfo()
            self.info = info
            // A connection means the camera is up: an update that ended in a
            // restart or a failure is over, whichever way it went.
            updater?.settle()
            cameraId = info.cameraId
            settings.cameraId = info.cameraId
            cameraName = info.deviceName
            log.add("Info: \(info.photoCount) photos, \(info.unsyncedCount) unsynced, boot \(info.boot)"
                    + (info.firmware.map { ", fw \($0)" } ?? ""))
            if info.onTrial {
                // It confirms itself after twenty seconds up (§3.7); this is
                // only worth a line in the log if someone goes looking.
                log.add("camera is running a firmware update on trial")
            }

            var secret = Keychain.secret(for: info.cameraId)
            if secret == nil {
                // First contact: the read below makes iOS show the passkey prompt
                // (the camera displays `pair 123456`). Bonded from then on.
                linkState = .pairing
                let s = try await link.readSecret()
                try Keychain.store(secret: s, for: info.cameraId)
                secret = s
                log.add("paired; secret stored")
            }
            guard let secretData = secret else { return }
            let secretHex = secretData.hexString
            server.credentials = ServerClient.Credentials(cameraId: info.cameraId, secretHex: secretHex)
            linkState = .connected(info.deviceName)

            // Trust on first use: registers the camera, or checks the secret. Cheap,
            // and its failure (no network) is not a reason to skip the BLE part.
            do {
                let hello = try await server.hello(cameraId: info.cameraId, secretHex: secretHex, firmware: info.firmware)
                log.add("hello: \(hello.bound?.handle ?? "not linked yet")")
                serverError = nil
                // Against the version this camera actually reported, not the
                // one the server last heard about.
                refreshFirmware()
            } catch {
                serverError = error.localizedDescription
                log.add("hello failed: \(error.localizedDescription)")
            }

            await sync.run(info: info)
        } catch is CancellationError {
            // Superseded by a newer connection; nothing to report.
        } catch {
            lastError = error.localizedDescription
            log.add("session: \(error.localizedDescription)")
            if case .pairing = linkState {
                linkState = .connecting
            }
        }
    }

    // MARK: Helpers

    private static func url(from string: String) -> URL {
        if let url = URL(string: string), url.scheme != nil {
            return url
        }
        // A literal that is known to parse; the only force unwrap in the app.
        return URL(string: Settings.defaultServerBaseURL)!
    }
}
