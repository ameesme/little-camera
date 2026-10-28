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

    private let server: ServerClient

    init(server: ServerClient) {
        self.server = server
    }

    var bound: BoundBlog? { status?.bound }
    var isLinked: Bool { status?.bound != nil }
    var claim: ClaimState? { status?.claim }
    var isMine: Bool { status?.claim == .approved }
    var subscribers: [Subscriber] { status?.subscribers ?? [] }
    var waiting: [Subscriber] { subscribers.filter { $0.status == "pending" } }
    var approved: [Subscriber] { subscribers.filter { $0.status == "approved" } }
    /// The release the server knows about, taken from /status so a refresh does not ask twice.
    var publishedFirmware: FirmwareRelease? { status?.firmware?.latest }

    func refresh() {
        perform { [self] in
            let fresh = try await server.status()
            status = fresh
            awaitingAvatar = fresh.avatar.requestedAt != nil && !fresh.avatar.hasAvatar
            if fresh.claim == .approved { await loadFeed(reset: true) }
        }
    }

    // MARK: Being let in (§4.9)

    func createBlog(name: String, email: String, handle: String) {
        perform { [self] in
            made = try await server.createBlog(name: name.trimmed, email: email.trimmed, handle: handle.trimmed)
            refresh()
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

    func block(_ subscriber: Subscriber) {
        perform { [self] in
            try await server.block(id: subscriber.id)
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
    }

    func seedDemo(status: StatusResponse, photos: [FeedPhoto]) {
        self.status = status
        self.photos = photos
    }

    func forget() {
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
