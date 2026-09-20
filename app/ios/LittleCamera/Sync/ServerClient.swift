import Foundation
import CryptoKit

// HTTP camera API (protocol §4). JSON keys are snake_case on the wire and
// camelCase here via .convertFromSnakeCase / .convertToSnakeCase, consistently.

struct BoundProfile: Codable, Equatable {
    let handle: String
    let name: String
    let url: String
    let photoCount: Int?
    let battery: Int?
}

struct HelloResponse: Codable {
    let cameraId: String
    let shortCode: String
    let bound: BoundProfile?
    let serverTime: Int
}

struct PhotoResponse: Codable {
    let id: String
    let status: String
    let kind: String
    let bound: BoundProfile?
}

struct AvatarState: Codable, Equatable {
    let requestedAt: Int?
    let hasAvatar: Bool
}

struct Subscriber: Codable, Equatable, Identifiable {
    let id: Int
    let email: String
    let name: String?
    let status: String
    let addedBy: String
}

/// A published firmware image (protocol §4.7). `updateAvailable` is only sent
/// by `/api/firmware/latest`; inside `/status` the surrounding block says it.
struct FirmwareRelease: Codable, Equatable {
    let version: String
    let channel: String
    let hardware: String
    let size: Int
    let sha256: String
    let url: String
    let notes: String?
    let releasedAt: Int?
    let updateAvailable: Bool?
}

struct FirmwareState: Codable, Equatable {
    let installed: String?
    let latest: FirmwareRelease?
    let updateAvailable: Bool
}

struct StatusResponse: Codable, Equatable {
    let cameraId: String
    let shortCode: String
    let bound: BoundProfile?
    let avatar: AvatarState
    /// Absent from a server older than firmware updates.
    let firmware: FirmwareState?
    let subscribers: [Subscriber]
    let serverTime: Int
}

struct ErrorBody: Codable {
    let error: String
    let message: String?
}

enum ServerError: LocalizedError {
    case notConfigured
    case badURL(String)
    case http(Int, String)
    case notHTTP
    case firmwareMismatch(String)

    var errorDescription: String? {
        switch self {
        case .notConfigured: return "no camera credentials yet"
        case .badURL(let s): return "bad server URL: \(s)"
        case .http(let code, let message): return "server \(code): \(message)"
        case .notHTTP: return "not an HTTP response"
        case .firmwareMismatch(let what): return "downloaded firmware does not match the release: \(what)"
        }
    }
}

/// What the bridge learned from POST /photos. 200 (duplicate), 201 (stored) and
/// 409 all mean "the server has this file": the photo must not be re-sent, and the
/// camera may be ACKed.
struct UploadOutcome {
    let statusCode: Int
    let response: PhotoResponse?

    var isDuplicate: Bool { statusCode == 409 || response?.status == "duplicate" }
    var kind: String { response?.kind ?? "photo" }
}

final class ServerClient {
    struct Credentials {
        let cameraId: String
        let secretHex: String
    }

    var baseURL: URL
    var credentials: Credentials?

    private let session: URLSession
    private let decoder: JSONDecoder
    private let encoder: JSONEncoder

    init(baseURL: URL) {
        self.baseURL = baseURL
        let config = URLSessionConfiguration.default
        config.timeoutIntervalForRequest = 30
        // Background syncs are short; failing fast beats waiting for connectivity.
        config.waitsForConnectivity = false
        session = URLSession(configuration: config)
        decoder = JSONDecoder()
        decoder.keyDecodingStrategy = .convertFromSnakeCase
        encoder = JSONEncoder()
        encoder.keyEncodingStrategy = .convertToSnakeCase
    }

    // MARK: Routes

    /// Trust on first use: creates the camera server-side, or checks the secret.
    /// `firmware` is what Info reported, so the server can say whether there is
    /// anything newer; a camera too old to say sends nothing.
    func hello(cameraId: String, secretHex: String, firmware: String?) async throws -> HelloResponse {
        struct Body: Encodable {
            let cameraId: String
            let secret: String
            let firmware: String?
        }
        var req = try request("POST", "hello", authenticated: false)
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = try encoder.encode(Body(cameraId: cameraId, secret: secretHex, firmware: firmware))
        let (code, data) = try await send(req)
        return try decode(HelloResponse.self, code: code, data: data)
    }

    // MARK: Firmware (§4.7)

    /// The newest release for this hardware, or nil when nothing is published.
    /// Unauthenticated, like the route: an image is not a secret.
    func latestFirmware(installed: String?) async throws -> FirmwareRelease? {
        let base = baseURL.appendingPathComponent("api/firmware/latest")
        guard var comps = URLComponents(url: base, resolvingAgainstBaseURL: false) else {
            throw ServerError.badURL(base.absoluteString)
        }
        if let installed = installed {
            comps.queryItems = [URLQueryItem(name: "installed", value: installed)]
        }
        guard let url = comps.url else { throw ServerError.badURL(base.absoluteString) }
        var req = URLRequest(url: url)
        req.httpMethod = "GET"
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        let (code, data) = try await send(req)
        if code == 404 { return nil }  // Nothing published for this hardware yet
        return try decode(FirmwareRelease.self, code: code, data: data)
    }

    /// The image itself, checked against the release before anyone is told it
    /// arrived. The camera checks it again after the BLE hop (§3.7) — this one
    /// catches a bad download early, so a megabyte is not pushed over
    /// Bluetooth to be rejected at the far end.
    func downloadFirmware(_ release: FirmwareRelease) async throws -> Data {
        guard let url = URL(string: release.url) else { throw ServerError.badURL(release.url) }
        var req = URLRequest(url: url)
        req.timeoutInterval = 120
        let (data, response) = try await session.data(for: req)
        guard let http = response as? HTTPURLResponse else { throw ServerError.notHTTP }
        guard http.statusCode == 200 else { throw ServerError.http(http.statusCode, "firmware download failed") }
        guard data.count == release.size else {
            throw ServerError.firmwareMismatch("\(data.count) bytes, expected \(release.size)")
        }
        let digest = Data(SHA256.hash(data: data)).hexString
        guard digest == release.sha256.lowercased() else { throw ServerError.firmwareMismatch("sha256") }
        return data
    }

    func uploadPhoto(pbm: Data, index: UInt16, capturedAt: UInt32?, source: String?) async throws -> UploadOutcome {
        var req = try request("POST", "photos")
        req.setValue("image/x-portable-bitmap", forHTTPHeaderField: "Content-Type")
        req.setValue(String(index), forHTTPHeaderField: "X-Photo-Index")
        if let capturedAt = capturedAt {
            req.setValue(String(capturedAt), forHTTPHeaderField: "X-Captured-At")
        }
        if let source = source {
            req.setValue(source, forHTTPHeaderField: "X-Captured-At-Source")
        }
        req.httpBody = pbm
        let (code, data) = try await send(req)
        switch code {
        case 200, 201:
            let body = try decoder.decode(PhotoResponse.self, from: data)
            return UploadOutcome(statusCode: code, response: body)
        case 409:
            // Conflict = already there. Body shape is not guaranteed; do not insist on it.
            let body = try? decoder.decode(PhotoResponse.self, from: data)
            return UploadOutcome(statusCode: code, response: body)
        default:
            throw ServerError.http(code, errorMessage(data))
        }
    }

    func status() async throws -> StatusResponse {
        let req = try request("GET", "status")
        let (code, data) = try await send(req)
        return try decode(StatusResponse.self, code: code, data: data)
    }

    func addSubscriber(email: String, name: String?) async throws -> Int {
        struct Body: Encodable {
            let email: String
            let name: String?
        }
        struct Reply: Decodable {
            let id: Int
        }
        var req = try request("POST", "subscribers")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = try encoder.encode(Body(email: email, name: name))
        let (code, data) = try await send(req)
        return try decode(Reply.self, code: code, data: data).id
    }

    func approve(id: Int) async throws {
        try await post("subscribers/\(id)/approve")
    }

    func block(id: Int) async throws {
        try await post("subscribers/\(id)/block")
    }

    /// Returns `requested_at`. The next photo captured after it becomes the avatar.
    func requestAvatar() async throws -> Int {
        struct Reply: Decodable {
            let requestedAt: Int
        }
        var req = try request("POST", "request-avatar")
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = Data("{}".utf8)
        let (code, data) = try await send(req)
        return try decode(Reply.self, code: code, data: data).requestedAt
    }

    // MARK: Plumbing

    private func post(_ path: String) async throws {
        var req = try request("POST", path)
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.httpBody = Data("{}".utf8)
        let (code, data) = try await send(req)
        guard (200..<300).contains(code) else { throw ServerError.http(code, errorMessage(data)) }
    }

    private func request(_ method: String, _ path: String, authenticated: Bool = true) throws -> URLRequest {
        var req = URLRequest(url: baseURL.appendingPathComponent("api/camera/" + path))
        req.httpMethod = method
        req.setValue("application/json", forHTTPHeaderField: "Accept")
        if authenticated {
            guard let c = credentials else { throw ServerError.notConfigured }
            req.setValue("Camera \(c.cameraId):\(c.secretHex)", forHTTPHeaderField: "Authorization")
        }
        return req
    }

    private func send(_ req: URLRequest) async throws -> (Int, Data) {
        let (data, response) = try await session.data(for: req)
        guard let http = response as? HTTPURLResponse else { throw ServerError.notHTTP }
        return (http.statusCode, data)
    }

    private func decode<T: Decodable>(_ type: T.Type, code: Int, data: Data) throws -> T {
        guard (200..<300).contains(code) else { throw ServerError.http(code, errorMessage(data)) }
        return try decoder.decode(type, from: data)
    }

    private func errorMessage(_ data: Data) -> String {
        if let body = try? decoder.decode(ErrorBody.self, from: data) {
            return body.message ?? body.error
        }
        return String(decoding: data.prefix(120), as: UTF8.self)
    }
}
