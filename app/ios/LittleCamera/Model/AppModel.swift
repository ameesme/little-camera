import Foundation

/// Which stage of the app the owner is in, and the three things it is made of.
@Observable @MainActor
final class AppModel {
    enum Stage {
        /// Nothing has happened yet: what the camera is, and the way in.
        case intro
        /// The way in has been pressed and iOS has not been asked for Bluetooth yet.
        case permission
        /// No camera chosen yet: pick the server and the camera.
        case setup
        /// Paired, and the camera is behind the firmware the server has published.
        case firmware
        /// The camera has a blog already, so the owner has to prove the blog is theirs.
        case claim
        /// No blog on this camera: make one, or sign in to one and bind it.
        case onboarding
        /// Everything is set up; the tabs take over.
        case ready
    }

    /// Set once the owner has read the camera's hello and pressed on, which is worth asking once and not at every launch.
    private var connectedSeen = false
    private var celebrating = false
    /// Set once the owner has waved the firmware offer away, so it does not block them twice.
    private var firmwareDeferred = false
    /// Mirrors the stored flag, because `stage` has to be recomputed the moment it changes.
    private var avatarSkipped: Bool
    /// Mirrors the stored flag for the same reason.
    private var setUp: Bool
    private var started = false
    /// `-demo <screen>` pins the stage so a screenshot can be taken of it; pressing the
    /// way in unpins it, so the flow can still be walked through on a simulator.
    private var demoStage: Stage? = Demo.stage

    let camera: CameraSession
    let blog: Blog
    let firmware: FirmwareUpdate
    let log = SyncLog()

    private(set) var serverURL: String

    private let settings: Settings
    private let server: ServerClient

    init() {
        let settings = Settings()
        self.settings = settings
        serverURL = settings.serverBaseURL
        avatarSkipped = settings.avatarSkipped
        setUp = settings.setUp
        server = ServerClient(baseURL: AppModel.url(from: settings.serverBaseURL))
        camera = CameraSession(settings: settings, server: server, log: log)
        blog = Blog(server: server)
        firmware = FirmwareUpdate(server: server, log: log)

        if let id = settings.cameraId, let secret = Keychain.secret(for: id) {
            server.credentials = ServerClient.Credentials(cameraId: id, secretHex: secret.hexString)
        }
        if let forced = Demo.stage {
            camera.seedDemo(info: Demo.info)
            blog.seedDemo(
                status: Demo.status(claim: forced == .claim ? .pending : .approved, bound: Demo.step == .avatar || (Demo.step == nil && forced != .onboarding)),
                photos: forced == .ready && Demo.name != "empty" ? Demo.photos : [],
                code: Demo.code,
                asked: Demo.asked
            )
            if let found = Demo.found { camera.seedFound(found, pairing: Demo.pairing) }
            firmware.seedDemo(release: Demo.release)
        }
    }

    var stage: Stage {
        if let demoStage { return demoStage }
        // Until a camera and a blog have found each other, every launch opens on the way in.
        if !started && !setUp { return .intro }
        if !camera.isStarted || !camera.radioAnswered { return .permission }
        // Not `isPaired`: that is only the camera this phone chose, and making a blog needs the secret it has not read yet.
        // Walking the way in also means the camera is in the room: a remembered secret is no reason to say hello to it,
        // and the way in ends on that hello rather than on the connection that caused it.
        if !camera.isLinked || (!setUp && (camera.info == nil || !connectedSeen)) { return .setup }
        if !firmwareDeferred && firmware.isAvailable(for: camera) { return .firmware }
        switch blog.claim {
        case .approved: return celebrating || (blog.needsAvatar && !avatarSkipped) ? .onboarding : .ready
        case .pending: return .claim
        case .unbound, .none: return .onboarding
        }
    }

    func confirmConnected() {
        connectedSeen = true
        camera.helloRead()
    }

    func deferFirmware() {
        firmwareDeferred = true
    }

    func getStarted() {
        demoStage = nil
        started = true
    }

    /// No camera answered, so the way in goes back to its first page instead of waiting for ever.
    func startOver() {
        started = false
    }

    /// The owner has read what the alert is for, and the central that raises it can exist now.
    func allowBluetooth() {
        camera.start(firmware: firmware)
    }

    /// The blog keeps its empty head and the tabs take over.
    func skipAvatar() {
        avatarSkipped = true
        settings.avatarSkipped = true
    }

    /// The picture landing is what would otherwise end onboarding; hold the stage for its last screen.
    func celebrate() {
        celebrating = true
    }

    func openBlog() {
        celebrating = false
        avatarSkipped = true
        settings.avatarSkipped = true
    }

    /// Logging out and disconnecting are the same act: this phone forgets the camera and the blog at once.
    func disconnect() {
        firmwareDeferred = false
        avatarSkipped = false
        celebrating = false
        setUp = false
        settings.setUp = false
        started = false
        blog.forget()
        camera.forget()
    }

    func start() {
        // A demo run is seeded data and nothing else: no radio, no server, nothing to overwrite it.
        guard Demo.stage == nil else { return }
        // The keychain outlives the app that wrote it, so a reinstall still holds the secrets of
        // whatever cameras the last install paired with. Nothing here remembers those cameras any
        // more, and a secret with no camera beside it would let the app skip pairing for one it has
        // never met — on a link the camera will not encrypt, because its own bond went with the app.
        if settings.cameraId == nil { Keychain.deleteEverySecret() }
        blog.onStatus = { [weak self] status in
            guard let self, status.claim == .approved, status.bound != nil else { return }
            setUp = true
            settings.setUp = true
        }
        camera.onAwake = { [weak self] in self?.firmware.settle() }
        camera.onAvatarTaken = { [weak self] in self?.blog.avatarTaken() }
        camera.onConnected = { [weak self] in self?.refresh() }
        // Creating the central is what asks for Bluetooth, so a first launch waits for the way in; a camera this phone has already paired cannot wait, because restored state only reaches a manager that exists.
        if settings.pairedPeripheralId != nil { camera.start(firmware: firmware) }
        refresh()
    }

    func refresh() {
        guard Demo.stage == nil else { return }
        blog.refresh()
        camera.reportTelemetry()
        firmware.refresh(installed: camera.info?.firmware, published: blog.publishedFirmware)
    }

    func useCamera(_ found: CameraSession.Found) {
        camera.use(found)
    }

    private static func url(from string: String) -> URL {
        if let url = URL(string: string), url.scheme != nil { return url }
        // A literal that is known to parse: the only force unwrap in the app.
        return URL(string: Settings.defaultServerBaseURL)!
    }
}
