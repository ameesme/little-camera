import Foundation

/// What the server says about this camera's blog: whether it is linked, who reads it, and the profile picture.
@Observable @MainActor
final class Blog {
    private(set) var status: StatusResponse?
    private(set) var error: String?
    private(set) var awaitingAvatar = false

    private let server: ServerClient

    init(server: ServerClient) {
        self.server = server
    }

    var bound: BoundProfile? { status?.bound }
    var isLinked: Bool { status?.bound != nil }
    var subscribers: [Subscriber] { status?.subscribers ?? [] }
    var waiting: [Subscriber] { subscribers.filter { $0.status == "pending" } }
    var approved: [Subscriber] { subscribers.filter { $0.status == "approved" } }
    /// The release the server knows about, taken from /status so a refresh does not ask twice.
    var publishedFirmware: FirmwareRelease? { status?.firmware?.latest }

    func refresh() {
        perform { [self] in
            status = try await server.status()
            // Pending means the server holds a request with no avatar against it yet.
            awaitingAvatar = status?.avatar.requestedAt != nil && status?.avatar.hasAvatar == false
        }
    }

    func add(email: String, name: String?) {
        let email = email.trimmingCharacters(in: .whitespaces)
        let name = name?.trimmingCharacters(in: .whitespaces)
        perform { [self] in
            _ = try await server.addSubscriber(email: email, name: (name?.isEmpty ?? true) ? nil : name)
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

    func requestAvatar() {
        perform { [self] in
            _ = try await server.requestAvatar()
            awaitingAvatar = true
        }
    }

    func avatarTaken() {
        awaitingAvatar = false
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
