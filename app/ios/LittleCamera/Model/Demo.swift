import Foundation

/// Stand-in data for `-demo <stage>`, so every screen can be looked at on a simulator,
/// which has no Bluetooth and so can never get past the first one on its own.
enum Demo {
    static var stage: AppModel.Stage? {
        guard let name = value(for: "-demo") else { return nil }
        switch name {
        case "firmware": return .firmware
        case "claim": return .claim
        case "onboarding": return .onboarding
        case "ready": return .ready
        default: return nil
        }
    }

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

    static func status(claim: ClaimState, bound: Bool) -> StatusResponse {
        StatusResponse(
            cameraId: "7cdfa1e2b3c4",
            shortCode: "MM48F3",
            bound: bound ? BoundBlog(handle: "mees", name: "Mees", url: "https://mees.lttl.cam", photoCount: 12) : nil,
            avatar: AvatarState(requestedAt: nil, hasAvatar: true),
            firmware: FirmwareState(installed: "0.2.0", latest: release, updateAvailable: true),
            claim: claim,
            subscribers: [
                Subscriber(id: 1, email: "sanne@example.com", name: "Sanne", status: "approved", addedBy: "owner"),
                Subscriber(id: 2, email: "joost@example.com", name: nil, status: "pending", addedBy: "self"),
                Subscriber(id: 3, email: "lotte@example.com", name: "Lotte", status: "approved", addedBy: "self"),
            ],
            serverTime: 1_761_600_000
        )
    }

    static let photos: [FeedPhoto] = [
        FeedPhoto(id: "5jk7ferd11ap", url: "", kind: "photo", capturedAt: 1_761_594_000, uploadedAt: 1_761_594_100,
                  comments: [FeedComment(id: 1, name: "Sanne", body: "that is the dog, surely"),
                             FeedComment(id: 2, name: "Mees", body: "it is the dog")]),
        FeedPhoto(id: "9mq2xd7bk0zz", url: "", kind: "photo", capturedAt: 1_761_500_000, uploadedAt: 1_761_500_200,
                  comments: []),
        FeedPhoto(id: "a1b2c3d4e5f6", url: "", kind: "avatar", capturedAt: 1_761_400_000, uploadedAt: 1_761_400_050,
                  comments: [FeedComment(id: 3, name: "Lotte", body: "hello")]),
    ]
}
