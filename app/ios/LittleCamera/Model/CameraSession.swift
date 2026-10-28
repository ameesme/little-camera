import CoreBluetooth
import CoreGraphics
import Foundation

/// The camera as this phone sees it: the link, what Info last said, and the sync that follows every connection.
@Observable @MainActor
final class CameraSession {
    enum State: Equatable {
        case off
        case scanning
        case connecting
        case pairing
        case connected(String)
        case syncing(done: Int, total: Int)
    }

    struct Found: Identifiable, Equatable {
        let id: UUID
        let name: String
    }

    private(set) var state: State = .off
    private(set) var isPaired: Bool
    private(set) var bluetooth: CBManagerState = .unknown
    private(set) var found: [Found] = []
    private(set) var info: CameraInfo?
    private(set) var name: String?
    private(set) var cameraId: String?
    private(set) var lastSyncAt: Date?
    private(set) var uploadedCount: Int
    private(set) var thumbnails: [CGImage] = []
    private(set) var error: String?

    /// Raised once the camera has said hello, which is when the server is worth asking about.
    var onConnected: (@MainActor () -> Void)?
    var onAvatarTaken: (@MainActor () -> Void)?
    /// Called at the start of every connection so a finished update can stop claiming to be running.
    var onAwake: (@MainActor () -> Void)?

    private let settings: Settings
    private let server: ServerClient
    private let log: SyncLog
    @ObservationIgnored private var link: CameraLink?
    @ObservationIgnored private var sync: SyncEngine?
    @ObservationIgnored private var task: Task<Void, Never>?

    init(settings: Settings, server: ServerClient, log: SyncLog) {
        self.settings = settings
        self.server = server
        self.log = log
        isPaired = settings.pairedPeripheralId != nil
        cameraId = settings.cameraId
        lastSyncAt = settings.lastSyncAt
        uploadedCount = settings.uploadedCount
    }

    var isReady: Bool { link?.isReady ?? false }
    var canUpdateFirmware: Bool { link?.canUpdate ?? false }
    var isSyncing: Bool { sync?.isRunning ?? false }

    var progress: Double {
        if case .syncing(let done, let total) = state, total > 0 { return Double(done) / Double(total) }
        return lastSyncAt == nil ? 0 : 1
    }

    var line: String {
        switch state {
        case .off: return bluetoothLine
        case .scanning, .connecting: return "Looking for your little camera"
        case .pairing: return "Type the number on its screen"
        case .connected(let name): return "Connected to \(name)"
        case .syncing(let done, let total): return "Sending pictures, \(done) of \(total)"
        }
    }

    private var bluetoothLine: String {
        switch bluetooth {
        case .poweredOff: return "Bluetooth is off."
        case .unauthorized: return "This app is not allowed to use Bluetooth."
        case .unsupported: return "This phone has no Bluetooth."
        default: return "Starting Bluetooth."
        }
    }

    /// The link must exist as early as possible: iOS hands restored state to the manager with our identifier only if it is already there.
    func start(firmware: FirmwareUpdate) {
        guard link == nil else { return }
        let link = CameraLink(pairedId: settings.pairedPeripheralId) { [weak self] event in self?.handle(event) }
        self.link = link
        let sync = SyncEngine(link: link, server: server, settings: settings, log: log)
        sync.onEvent = { [weak self] event in self?.handle(event) }
        self.sync = sync
        firmware.attach(link: link)
        link.start()
    }

    func use(_ camera: Found) {
        settings.pairedPeripheralId = camera.id
        isPaired = true
        name = camera.name
        log.add("using \(camera.name)")
        link?.use(peripheralId: camera.id)
    }

    /// Relays what Info said to the server (§4.8), so the blog and the app show the camera's own numbers.
    func reportTelemetry() {
        guard let info, server.credentials != nil else { return }
        Task { @MainActor in
            do { try await server.sendTelemetry(info) } catch { log.add("telemetry: \(error.localizedDescription)") }
        }
    }

    func seedFound(_ camera: Found, pairing: Bool = false) {
        found = [camera]
        state = pairing ? .pairing : .scanning
    }

    func seedDemo(info: CameraInfo) {
        bluetooth = .poweredOn
        self.info = info
        cameraId = info.cameraId
        name = info.deviceName
        isPaired = true
        state = .connected(info.deviceName)
    }

    /// This phone lets the camera go: the pairing, the secret and everything read off it.
    func forget() {
        if let cameraId { Keychain.delete(for: cameraId) }
        settings.forgetCamera()
        isPaired = false
        info = nil
        name = nil
        cameraId = nil
        thumbnails = []
        lastSyncAt = nil
        uploadedCount = 0
        error = nil
        state = .scanning
        found = []
        link?.forget()
    }

    /// The owner pressing SYNC NOW. A sleeping camera has no radio, so there is nothing to do but ask them to wake it.
    func syncNow() {
        guard isReady else {
            error = "Little camera is asleep. Press its button and try again."
            log.add("camera not connected; press its shutter to wake it")
            return
        }
        begin()
    }

    private func handle(_ event: CameraLink.Event) {
        switch event {
        case .bluetooth(let value):
            bluetooth = value
            if value != .poweredOn { state = .off }
        case .scanning:
            state = .scanning
        case .found(let id, let name):
            if !found.contains(where: { $0.id == id }) { found.append(Found(id: id, name: name)) }
        case .connecting(let peripheral):
            state = .connecting
            name = name ?? peripheral
        case .ready(let peripheral):
            name = peripheral
            log.add("connected to \(peripheral)")
            begin()
        case .disconnected(let reason):
            log.add("disconnected" + (reason.map { ": \($0)" } ?? ""))
            // The link re-arms its own connect, so from here a disconnect just looks like waiting again.
            state = isPaired ? .connecting : .scanning
        case .log(let line):
            log.add(line)
        }
    }

    private func handle(_ event: SyncEngine.Event) {
        switch event {
        case .progress(let done, let total):
            state = .syncing(done: done, total: total)
        case .uploaded(_, let image, let kind):
            uploadedCount = settings.uploadedCount
            if let image { thumbnails = Array((thumbnails + [image]).suffix(4)) }
            if kind == "avatar" { onAvatarTaken?() }
        case .finished(_, let latest):
            info = latest
            lastSyncAt = settings.lastSyncAt
            error = nil
            state = .connected(latest.deviceName)
            onConnected?()
        case .failed(let message):
            error = message
            if isReady { state = .connected(name ?? "camera") }
        }
    }

    private func begin() {
        task?.cancel()
        task = Task { [weak self] in await self?.run() }
    }

    /// One connection: read Info, pair if this is the first time, say hello, then sync.
    private func run() async {
        guard let link, let sync else { return }
        do {
            let info = try await link.readInfo()
            self.info = info
            cameraId = info.cameraId
            settings.cameraId = info.cameraId
            name = info.deviceName
            onAwake?()
            log.add("Info: \(info.photoCount) photos, \(info.unsyncedCount) unsynced, boot \(info.boot), fw \(info.firmware)")
            if info.onTrial { log.add("camera is running a firmware update on trial") }

            let secret = try await secret(for: info.cameraId, link: link)
            server.credentials = ServerClient.Credentials(cameraId: info.cameraId, secretHex: secret.hexString)
            state = .connected(info.deviceName)
            await sayHello(info: info, secretHex: secret.hexString)
            await sync.run(info: info)
        } catch is CancellationError {
            // A newer connection took over.
        } catch {
            self.error = error.localizedDescription
            log.add("session: \(error.localizedDescription)")
            if state == .pairing { state = .connecting }
        }
    }

    /// Reading the encrypted characteristic is what makes iOS show its passkey prompt, so first contact pairs here.
    private func secret(for cameraId: String, link: CameraLink) async throws -> Data {
        if let stored = Keychain.secret(for: cameraId) { return stored }
        state = .pairing
        let fresh = try await link.readSecret()
        try Keychain.store(secret: fresh, for: cameraId)
        log.add("paired; secret stored")
        return fresh
    }

    /// Failing here means no network, which is no reason to skip the Bluetooth half of the session.
    private func sayHello(info: CameraInfo, secretHex: String) async {
        do {
            let hello = try await server.hello(cameraId: info.cameraId, secretHex: secretHex, firmware: info.firmware)
            log.add("hello: \(hello.bound?.handle ?? "not linked yet")")
            onConnected?()
        } catch {
            log.add("hello failed: \(error.localizedDescription)")
        }
    }
}
