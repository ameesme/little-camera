import Foundation

/// Which stage of the app the owner is in, and the three things it is made of.
@Observable @MainActor
final class AppModel {
    enum Stage {
        /// Nothing has happened yet: what the camera is, and the way in.
        case intro
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

    /// Set once the owner has waved the firmware offer away, so it does not block them twice.
    private var firmwareDeferred = false
    /// Mirrors the stored flag, because `stage` has to be recomputed the moment it changes.
    private var avatarSkipped: Bool
    /// Mirrors the stored flag for the same reason.
    private var setUp: Bool
    private var started = false

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
                photos: forced == .ready ? Demo.photos : [],
                code: Demo.code,
                asked: Demo.asked
            )
            if let found = Demo.found { camera.seedFound(found, pairing: Demo.pairing) }
            firmware.seedDemo(release: Demo.release)
        }
    }

    var stage: Stage {
        if let forced = Demo.stage { return forced }
        // Until a camera and a blog have found each other, every launch opens on the way in.
        if !started && !setUp { return .intro }
        if !camera.isPaired { return .setup }
        if !firmwareDeferred && firmware.isAvailable(for: camera) { return .firmware }
        switch blog.claim {
        case .approved: return blog.needsAvatar && !avatarSkipped ? .onboarding : .ready
        case .pending: return .claim
        case .unbound, .none: return .onboarding
        }
    }

    func deferFirmware() {
        firmwareDeferred = true
    }

    func getStarted() {
        started = true
    }

    /// The blog keeps its empty head and the tabs take over.
    func skipAvatar() {
        avatarSkipped = true
        settings.avatarSkipped = true
    }

    /// Logging out and disconnecting are the same act: this phone forgets the camera and the blog at once.
    func disconnect() {
        firmwareDeferred = false
        avatarSkipped = false
        setUp = false
        settings.setUp = false
        started = false
        blog.forget()
        camera.forget()
    }

    func start() {
        // A demo run is seeded data and nothing else: no radio, no server, nothing to overwrite it.
        guard Demo.stage == nil else { return }
        blog.onStatus = { [weak self] status in
            guard let self, status.claim == .approved, status.bound != nil else { return }
            setUp = true
            settings.setUp = true
        }
        camera.onAwake = { [weak self] in self?.firmware.settle() }
        camera.onAvatarTaken = { [weak self] in self?.blog.avatarTaken() }
        camera.onConnected = { [weak self] in self?.refresh() }
        camera.start(firmware: firmware)
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
