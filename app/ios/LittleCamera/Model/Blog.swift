import Foundation

/// What the server says about this camera's blog: whether it has one, whether this phone is allowed
/// to see it, what is on it, and who reads it.
@Observable @MainActor
final class Blog {
    private(set) var status: StatusResponse?
    private(set) var error: String?
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
    /// The blog this camera is being set up for but has not bound yet (§4.4). The server remembers it,
    /// so a phone reopened mid-onboarding resumes rather than making the same owner a second blog.
    var pending: PendingBlog? { status?.pending }
    /// An address whose owner has opened the link but has not named a blog for it yet (§4.4).
    var confirmedEmail: String? { status?.confirmedEmail }
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
            if fresh.claim == .approved { await loadFeed(reset: true) }
        }
    }

    // MARK: Being let in (§4.9)

    func createBlog(name: String, handle: String) {
        takenEmail = false
        perform { [self] in
            do {
                made = try await server.createBlog(name: name.trimmed, handle: handle.trimmed)
                refresh()
            } catch ServerError.http(_, _, "email_taken") {
                // The address grew a blog between confirming it and naming one: back to the address screen,
                // where asking about it again mails a way into the blog that now owns it.
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

    /// The one question asked of an unbound camera: the owner's address. The server mails a way into the
    /// blog that address already owns, or a way to confirm it and start one, and says nothing about which.
    func useAddress(_ email: String) {
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

    func comment(on photo: FeedPhoto, body: String) {
        perform { [self] in
            try await server.comment(photoId: photo.id, body: body)
            await loadFeed(reset: true)
        }
    }

    /// Gone from the list before the server has answered: the picture is the owner's to drop.
    func delete(_ photo: FeedPhoto) {
        photos.removeAll { $0.id == photo.id }
        perform { [self] in try await server.deletePhoto(id: photo.id) }
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

    /// Asked for the moment the step opens: the screen already says it is waiting, so there
    /// is nothing left for a button to add.
    func requestAvatar() {
        perform { [self] in _ = try await server.requestAvatar() }
    }

    func avatarTaken() {
        refresh()
    }

    func seedDemo(status: StatusResponse, photos: [FeedPhoto], code: String?, asked: Bool) {
        self.status = status
        self.photos = photos
        verificationCode = code
        askedToBeLetIn = asked
        made = Demo.made
        availability = Demo.availability
        takenEmail = Demo.takenEmail
        error = Demo.blogError
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
