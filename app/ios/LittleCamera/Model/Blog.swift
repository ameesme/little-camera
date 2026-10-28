import Foundation

/// What the server says about this camera's blog: whether it has one, whether this phone is allowed
/// to see it, what is on it, and who reads it.
@Observable @MainActor
final class Blog {
    private(set) var status: StatusResponse?
    private(set) var error: String?
    private(set) var awaitingAvatar = false
    private(set) var photos: [FeedPhoto] = []
    private(set) var nextPage: String?
    private(set) var askedToBeLetIn = false
    private(set) var made: ServerClient.MadeBlog?
    private(set) var verificationCode: String?
    /// Set when the server refused the blog for a reason a screen can act on, rather than only report.
    private(set) var takenEmail = false
    private(set) var availability: Availability = .unknown

    enum Availability {
        case unknown
        case checking
        case free
        case taken
    }

    /// Raised on every answer from /status, so the app can remember that this phone is set up.
    var onStatus: (@MainActor (StatusResponse) -> Void)?

    private let server: ServerClient

    init(server: ServerClient) {
        self.server = server
    }

    var bound: BoundBlog? { status?.bound }
    var isLinked: Bool { status?.bound != nil }
    var claim: ClaimState? { status?.claim }
    /// The blog has no picture at its head yet, which is the last thing onboarding asks for.
    var needsAvatar: Bool { status.map { !$0.avatar.hasAvatar } ?? false }
    var subscribers: [Subscriber] { status?.subscribers ?? [] }
    var waiting: [Subscriber] { subscribers.filter { $0.status == "pending" } }
    var approved: [Subscriber] { subscribers.filter { $0.status == "approved" } }
    /// The release the server knows about, taken from /status so a refresh does not ask twice.
    var publishedFirmware: FirmwareRelease? { status?.firmware?.latest }

    func refresh() {
        perform { [self] in
            let fresh = try await server.status()
            status = fresh
            onStatus?(fresh)
            awaitingAvatar = fresh.avatar.requestedAt != nil && !fresh.avatar.hasAvatar
            if fresh.claim == .approved { await loadFeed(reset: true) }
        }
    }

    // MARK: Being let in (§4.9)

    func createBlog(name: String, email: String, handle: String) {
        takenEmail = false
        perform { [self] in
            do {
                made = try await server.createBlog(name: name.trimmed, email: email.trimmed, handle: handle.trimmed)
                refresh()
            } catch ServerError.http(_, _, "email_taken") {
                // Not an error so much as a wrong turn: that address already has a blog to sign into.
                takenEmail = true
            }
        }
    }

    /// For a camera that already has a blog: the owner says yes by email, and `claim` turns approved.
    func askToBeLetIn() {
        perform { [self] in
            try await server.requestClaim(email: nil)
            askedToBeLetIn = true
        }
    }

    /// For a camera with no blog yet: name the blog by its owner's address.
    func signIn(email: String) {
        perform { [self] in
            try await server.requestClaim(email: email.trimmed)
            askedToBeLetIn = true
        }
    }

    func checkHandle(_ handle: String) async {
        guard Demo.stage == nil else { return }
        guard Handle.isValid(handle) else {
            availability = handle.isEmpty ? .unknown : .taken
            return
        }
        availability = .checking
        guard let free = try? await server.isHandleFree(handle) else {
            availability = .unknown
            return
        }
        availability = free ? .free : .taken
    }

    /// Asked for once the blog exists; the camera photographs it off this screen.
    func fetchVerificationCode() {
        guard verificationCode == nil else { return }
        perform { [self] in verificationCode = try await server.verificationCode().code }
    }

    // MARK: The blog itself

    func loadFeed(reset: Bool) async {
        do {
            let page = try await server.feed(before: reset ? nil : nextPage)
            photos = reset ? page.photos : photos + page.photos
            nextPage = page.next
            error = nil
        } catch {
            self.error = error.localizedDescription
        }
    }

    func loadMore() {
        guard nextPage != nil else { return }
        perform { [self] in await loadFeed(reset: false) }
    }

    func add(email: String, name: String?) {
        let name = name?.trimmed
        perform { [self] in
            _ = try await server.addSubscriber(email: email.trimmed, name: (name?.isEmpty ?? true) ? nil : name)
            refresh()
        }
    }

    func approve(_ subscriber: Subscriber) {
        perform { [self] in
            try await server.approve(id: subscriber.id)
            refresh()
        }
    }

    func remove(_ subscriber: Subscriber) {
        perform { [self] in
            try await server.removeSubscriber(id: subscriber.id)
            refresh()
        }
    }

    func requestAvatar() {
        perform { [self] in
            _ = try await server.requestAvatar()
            awaitingAvatar = true
        }
    }

    func avatarTaken() {
        awaitingAvatar = false
        refresh()
    }

    func seedDemo(status: StatusResponse, photos: [FeedPhoto], code: String?, asked: Bool) {
        self.status = status
        self.photos = photos
        verificationCode = code
        askedToBeLetIn = asked
        made = Demo.made
        availability = Demo.typedHandle.isEmpty ? .unknown : .free
        awaitingAvatar = Demo.name == "avatar"
    }

    func forget() {
        takenEmail = false
        status = nil
        photos = []
        nextPage = nil
        askedToBeLetIn = false
        made = nil
        verificationCode = nil
        error = nil
    }

    /// Every call to the server reaches the owner the same way: it worked, or the screen says why not.
    private func perform(_ work: @escaping () async throws -> Void) {
        guard server.credentials != nil else { return }
        Task { @MainActor in
            do {
                try await work()
                error = nil
            } catch {
                self.error = error.localizedDescription
            }
        }
    }
}

extension String {
    var trimmed: String { trimmingCharacters(in: .whitespacesAndNewlines) }
}

/// The web address a blog lives at. Mirrors `server/src/lib/handles.ts`, because asking the
/// server on every keystroke to decide whether a character is allowed is a round trip for nothing.
enum Handle {
    static let reserved: Set<String> = ["www", "api", "dev", "mail", "admin", "app"]

    /// Lowercase, letters, digits and hyphens. Typing anything else simply does not land.
    static func filter(_ input: String) -> String {
        String(input.lowercased().filter { $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "-") }.prefix(31))
    }

    static func isValid(_ handle: String) -> Bool {
        guard (2...31).contains(handle.count), let first = handle.first, first != "-" else { return false }
        return !reserved.contains(handle)
    }
}
