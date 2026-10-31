import CoreBluetooth
import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

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
    /// Chosen is not paired: this turns true when the owner has let the phone in and the secret is on it.
    private(set) var isLinked: Bool
    private(set) var bluetooth: CBManagerState = .unknown
    /// The central exists, which is also the moment iOS asks for Bluetooth.
    private(set) var isStarted = false
    private(set) var found: [Found] = []
    private(set) var info: CameraInfo?
    private(set) var name: String?
    private(set) var cameraId: String?
    private(set) var lastSyncAt: Date?
    private(set) var uploadedCount: Int
    private(set) var thumbnails: [CGImage] = []
    private(set) var avatarPNG: String?
    private(set) var error: String?
    /// The owner dismissed the pairing box. Reading the secret again just puts it straight back up,
    /// so nothing is asked of them until they say they are ready.
    private(set) var declined = false
    @ObservationIgnored private var demoSyncing = false
    @ObservationIgnored private var lastPrompt: CameraPrompt = .none

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
        isLinked = settings.cameraId.flatMap { Keychain.secret(for: $0) } != nil
        cameraId = settings.cameraId
        lastSyncAt = settings.lastSyncAt
        uploadedCount = settings.uploadedCount
    }

    /// iOS has answered the permission question, one way or the other. Until it has, there is nothing to say about the radio.
    var radioAnswered: Bool { bluetooth != .unknown }
    var isReady: Bool { link?.isReady ?? false }
    var canUpdateFirmware: Bool { link?.canUpdate ?? false }
    var isSyncing: Bool { demoSyncing || (sync?.isRunning ?? false) }

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
        isStarted = true
    }

    func use(_ camera: Found) {
        settings.pairedPeripheralId = camera.id
        isPaired = true
        name = camera.name
        log.add("using \(camera.name)")
        link?.use(peripheralId: camera.id)
    }

    /// Tell the camera which photo this phone is standing by for (§3.3), so its own screen
    /// can say so. Best effort: a camera that is asleep or out of range simply misses it,
    /// and the onboarding step it belongs to works exactly as it did before.
    func prompt(for step: OnboardStage.Step) {
        let want: CameraPrompt = step == .avatar ? .selfie : .none
        guard isReady, want != lastPrompt else { return }
        lastPrompt = want
        Task { @MainActor in
            do { try await link?.command(.prompt(want)) } catch { log.add("prompt: \(error.localizedDescription)") }
        }
    }

    /// The phone is showing the owner that the two have found each other; the camera says it too (§3.3).
    func sayHello() {
        guard isReady else { return }
        Task { @MainActor in
            do { try await link?.command(.prompt(.hello)) } catch { log.add("hello: \(error.localizedDescription)") }
        }
    }

    /// The owner has read the hello on the phone, so the panel can go back to whatever it was showing.
    func helloRead() {
        guard isReady else { return }
        Task { @MainActor in
            do { try await link?.command(.prompt(.none)) } catch { log.add("hello: \(error.localizedDescription)") }
        }
    }

    /// The camera has been asked for a photo and the owner is taking it now, so the phone watches
    /// for it instead of waiting for the next connection: nothing on the link tells it a picture
    /// landed, and the camera's radio goes down long before the owner reconnects. Info is a read,
    /// which costs almost nothing next to the stream it decides whether to start.
    func catchUp() {
        guard isReady, let link, let sync, !sync.isRunning else { return }
        Task { @MainActor in
            do {
                let fresh = try await link.readInfo()
                info = fresh
                guard fresh.unsyncedCount > 0 else { return }
                await sync.run(info: fresh)
            } catch {
                log.add("catch up: \(error.localizedDescription)")
            }
        }
    }

    /// Every picture still on the camera, uploaded again: the way a camera follows its owner to a new blog.
    func resyncEverything() {
        guard isReady, let link, let sync, !sync.isRunning else {
            error = "Little camera is asleep. Hold its button and try again."
            return
        }
        Task { @MainActor in
            do {
                let fresh = try await link.readInfo()
                info = fresh
                await sync.run(info: fresh, everything: true)
            } catch {
                log.add("resync: \(error.localizedDescription)")
            }
        }
    }

    /// The owner is ready to be asked again.
    func retryPairing() {
        declined = false
        error = nil
        begin()
    }

    /// A step on the phone the camera should be noticed for (§3.3). Best effort, like every prompt.
    func chirp(times: UInt8 = 1) {
        guard isReady else { return }
        Task { @MainActor in
            do { try await link?.command(.chirp(times: times)) } catch { log.add("chirp: \(error.localizedDescription)") }
        }
    }

    private static func png(_ image: CGImage) -> String? {
        let data = NSMutableData()
        guard let out = CGImageDestinationCreateWithData(data, UTType.png.identifier as CFString, 1, nil) else { return nil }
        CGImageDestinationAddImage(out, image, nil)
        guard CGImageDestinationFinalize(out) else { return nil }
        return (data as Data).base64EncodedString()
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
        bluetooth = Demo.bluetoothOff ? .poweredOff : .poweredOn
        self.info = info
        cameraId = info.cameraId
        name = info.deviceName
        isPaired = true
        isLinked = true
        isStarted = true
        demoSyncing = Demo.syncing
        error = Demo.cameraError
        state = Demo.bluetoothOff ? .off : demoSyncing ? .syncing(done: 5, total: 12) : .connected(info.deviceName)
        lastSyncAt = Demo.lastSyncAt
    }

    /// This phone lets the camera go: the pairing, the secret and everything read off it.
    func forget() {
        if let cameraId { Keychain.delete(for: cameraId) }
        settings.forgetCamera()
        isPaired = false
        isLinked = false
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
            error = "Little camera is asleep. Hold its button and try again."
            log.add("camera not connected; hold its shutter to wake it")
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
        case .pairingLost:
            error = "This phone still holds a pairing the camera has forgotten. Forget it in Settings, under Bluetooth, then come back."
            state = .scanning
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
            if kind == "avatar" {
                if let image { avatarPNG = CameraSession.png(image) }
                onAvatarTaken?()
            }
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
        guard let link, let sync, !declined else { return }
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
    /// Only first contact: a camera whose secret is already on this phone is never read again, because the read
    /// would ask iOS to encrypt, and a link that cannot be encrypted is dropped by the camera and retried by us,
    /// for ever. A bond that has gone stale surfaces as `pairingLost` on the connect instead.
    private func secret(for cameraId: String, link: CameraLink) async throws -> Data {
        if let stored = Keychain.secret(for: cameraId) {
            isLinked = true
            return stored
        }
        state = .pairing
        do {
            let fresh = try await link.readSecret()
            try Keychain.store(secret: fresh, for: cameraId)
            isLinked = true
            log.add("paired; secret stored")
            return fresh
        } catch {
            declined = true
            self.error = "This phone was not let in. Type the six numbers from the camera's screen into the pairing box."
            throw error
        }
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
