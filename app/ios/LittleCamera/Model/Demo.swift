import Foundation

/// Stand-in data for `-demo <stage>`, so every screen can be looked at on a simulator,
/// which has no Bluetooth and so can never get past the first one on its own.
enum Demo {
    static let name: String? = value(for: "-demo")

    static var stage: AppModel.Stage? {
        switch name {
        case "intro": return .intro
        case "setup", "found", "pairing": return .setup
        case "firmware": return .firmware
        case "claim", "asked": return .claim
        case "onboarding", "create", "handle", "signin", "sent", "bind", "avatar": return .onboarding
        case "ready", "empty", "camera", "people", "nobody": return .ready
        default: return nil
        }
    }

    /// Where in the onboarding stage to start, so each screen of it can be looked at on its own.
    static var step: OnboardStage.Step? {
        switch name {
        case "create": return .name
        case "handle": return .handle
        case "signin": return .signIn
        case "sent": return .sent
        case "bind": return .bind
        case "avatar": return .avatar
        default: return nil
        }
    }

    static var found: CameraSession.Found? {
        name == "found" || name == "pairing" ? CameraSession.Found(id: UUID(), name: "lc-8f21") : nil
    }

    static var pairing: Bool { name == "pairing" }
    static var onCameraTab: Bool { name == "camera" }
    static var showingPeople: Bool { name == "people" || name == "nobody" }

    static var asked: Bool { name == "asked" || name == "sent" }

    /// What the owner would have typed by the time each screen shows up.
    static var typedName: String { stage == nil ? "" : "Mees" }
    static var typedHandle: String { step == .handle ? "mees" : "" }

    static var made: ServerClient.MadeBlog? {
        step == .bind ? ServerClient.MadeBlog(handle: "mees", url: "https://mees.lttl.cam") : nil
    }

    static let code = "4821-9903-7715"

    private static func value(for flag: String) -> String? {
        let args = ProcessInfo.processInfo.arguments
        guard let i = args.firstIndex(of: flag), i + 1 < args.count else { return nil }
        return args[i + 1]
    }

    static let info = try! CameraInfo(
        data: Data([
            3,
            0x7c, 0xdf, 0xa1, 0xe2, 0xb3, 0xc4,
            12, 0, 3, 0, 57, 0, 17, 0,
            0x04, 0x03, 0x02, 0x01,
            0x08, 0x2c, 0xc5, 0x68,
            0x03,
            0, 2, 0,
            0,
            0x00, 0x00, 0x33, 0x00,
            0xEC, 0x0E,
            44,
            0x87, 0xd6, 0x12, 0x00,
            0x00, 0x00, 0x64, 0x00,
            200,
            0x8d, 0x5f, 0x01, 0x00,
        ])
    )

    static let release = FirmwareRelease(
        version: "0.3.0", channel: "stable", hardware: "v3q", size: 1_481_712,
        sha256: String(repeating: "a", count: 64), url: "https://lttl.cam/api/firmware/v3q/0.3.0",
        notes: "Sleeps sooner, chirps less on a flat battery.", releasedAt: 1_757_789_000, updateAvailable: true
    )

    static var hasAvatar: Bool { name != "avatar" }

    static func status(claim: ClaimState, bound: Bool) -> StatusResponse {
        StatusResponse(
            cameraId: "7cdfa1e2b3c4",
            bound: bound ? BoundBlog(handle: "mees", name: "Mees", url: "https://mees.lttl.cam", photoCount: 12) : nil,
            avatar: AvatarState(requestedAt: nil, hasAvatar: hasAvatar),
            firmware: FirmwareState(installed: "0.2.0", latest: release, updateAvailable: true),
            claim: claim,
            subscribers: name == "nobody" ? [] : [
                Subscriber(id: 1, email: "sanne@example.com", name: "Sanne", status: "approved", addedBy: "owner"),
                Subscriber(id: 2, email: "joost@example.com", name: nil, status: "pending", addedBy: "self"),
                Subscriber(id: 3, email: "lotte@example.com", name: "Lotte", status: "approved", addedBy: "self"),
            ],
            serverTime: 1_761_600_000
        )
    }

    /// Fixtures from `packages/pbm/fixtures`, dithered by the same tool the camera's preview uses.
    private static func bundled(_ name: String) -> String {
        Bundle.main.url(forResource: name, withExtension: "png")?.absoluteString ?? ""
    }

    static let photos: [FeedPhoto] = [
        FeedPhoto(id: "5jk7ferd11ap", url: bundled("demo-1"), kind: "photo", capturedAt: 1_761_594_000, uploadedAt: 1_761_594_100,
                  comments: [FeedComment(id: 1, name: "Sanne", body: "that is the dog, surely"),
                             FeedComment(id: 2, name: "Mees", body: "it is the dog")]),
        FeedPhoto(id: "9mq2xd7bk0zz", url: bundled("demo-2"), kind: "photo", capturedAt: 1_761_500_000, uploadedAt: 1_761_500_200,
                  comments: []),
        FeedPhoto(id: "a1b2c3d4e5f6", url: bundled("demo-3"), kind: "photo", capturedAt: 1_761_400_000, uploadedAt: 1_761_400_050,
                  comments: [FeedComment(id: 3, name: "Lotte", body: "hello")]),
    ]
}
