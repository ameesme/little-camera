import Foundation

/// Which stage of the app the owner is in, and the three things it is made of.
@Observable @MainActor
final class AppModel {
    enum Stage {
        /// No camera chosen yet: pick the server and the camera.
        case setup
        /// Paired with a camera the server has not tied to a blog yet.
        case linking
        /// Everything is set up; the screens take over.
        case ready
    }

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
        server = ServerClient(baseURL: AppModel.url(from: settings.serverBaseURL))
        camera = CameraSession(settings: settings, server: server, log: log)
        blog = Blog(server: server)
        firmware = FirmwareUpdate(server: server, log: log)

        if let id = settings.cameraId, let secret = Keychain.secret(for: id) {
            server.credentials = ServerClient.Credentials(cameraId: id, secretHex: secret.hexString)
        }
    }

    var stage: Stage {
        if !camera.isPaired { return .setup }
        return blog.isLinked ? .ready : .linking
    }

    func start() {
        camera.onAwake = { [weak self] in self?.firmware.settle() }
        camera.onAvatarTaken = { [weak self] in self?.blog.avatarTaken() }
        camera.onConnected = { [weak self] in self?.refresh() }
        camera.start(firmware: firmware)
        refresh()
    }

    func refresh() {
        blog.refresh()
        firmware.refresh(installed: camera.info?.firmware, published: blog.publishedFirmware)
    }

    func useCamera(_ found: CameraSession.Found, serverURL: String) {
        setServerURL(serverURL)
        camera.use(found)
    }

    func setServerURL(_ string: String) {
        let trimmed = string.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        settings.serverBaseURL = trimmed
        serverURL = trimmed
        server.baseURL = AppModel.url(from: trimmed)
    }

    private static func url(from string: String) -> URL {
        if let url = URL(string: string), url.scheme != nil { return url }
        // A literal that is known to parse: the only force unwrap in the app.
        return URL(string: Settings.defaultServerBaseURL)!
    }
}
