import Foundation
import Security

/// A plain UserDefaults wrapper, deliberately not observable: AppModel mirrors whatever the UI needs.
final class Settings {
    static let defaultServerBaseURL = "http://192.168.50.42.nip.io:3000"

    private let defaults: UserDefaults

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    private enum Key {
        static let serverBaseURL = "serverBaseURL"
        static let cameraId = "cameraId"
        static let pairedPeripheralId = "pairedPeripheralId"
        static let uploadedIndices = "uploadedIndices"
        static let uploadedHashes = "uploadedHashes"
        static let uploadedCount = "uploadedCount"
        static let lastSyncAt = "lastSyncAt"
        static let avatarSkipped = "avatarSkipped"
        static let setUp = "setUp"
    }

    var serverBaseURL: String {
        get { defaults.string(forKey: Key.serverBaseURL) ?? Settings.defaultServerBaseURL }
        set { defaults.set(newValue, forKey: Key.serverBaseURL) }
    }

    var cameraId: String? {
        get { defaults.string(forKey: Key.cameraId) }
        set { defaults.set(newValue, forKey: Key.cameraId) }
    }

    /// CoreBluetooth's identifier for the camera on this phone. It is not the MAC: iOS hides that, and the identifier differs per phone.
    var pairedPeripheralId: UUID? {
        get { defaults.string(forKey: Key.pairedPeripheralId).flatMap { UUID(uuidString: $0) } }
        set { defaults.set(newValue?.uuidString, forKey: Key.pairedPeripheralId) }
    }

    var uploadedIndices: [Int] {
        get { defaults.array(forKey: Key.uploadedIndices) as? [Int] ?? [] }
        set { defaults.set(newValue, forKey: Key.uploadedIndices) }
    }

    /// SHA-256 hex of files the server has confirmed. Lets a re-run skip the upload of a photo whose ACK was lost, without another round trip.
    var uploadedHashes: [String] {
        get { defaults.array(forKey: Key.uploadedHashes) as? [String] ?? [] }
        set { defaults.set(newValue, forKey: Key.uploadedHashes) }
    }

    var uploadedCount: Int {
        get { defaults.integer(forKey: Key.uploadedCount) }
        set { defaults.set(newValue, forKey: Key.uploadedCount) }
    }

    /// True once a camera and a blog have been through the whole way in, so a launch
    /// after that opens on the blog rather than on the way in again.
    var setUp: Bool {
        get { defaults.bool(forKey: Key.setUp) }
        set { defaults.set(newValue, forKey: Key.setUp) }
    }

    /// The owner waved the blog's picture away. Asked once, not at every launch.
    var avatarSkipped: Bool {
        get { defaults.bool(forKey: Key.avatarSkipped) }
        set { defaults.set(newValue, forKey: Key.avatarSkipped) }
    }

    var lastSyncAt: Date? {
        get { defaults.object(forKey: Key.lastSyncAt) as? Date }
        set { defaults.set(newValue, forKey: Key.lastSyncAt) }
    }

    /// Everything this phone knew about the camera it was paired with. The secret is in the keychain, not here.
    func forgetCamera() {
        for key in [Key.cameraId, Key.pairedPeripheralId, Key.uploadedIndices, Key.uploadedHashes, Key.uploadedCount, Key.lastSyncAt, Key.avatarSkipped, Key.setUp] {
            defaults.removeObject(forKey: key)
        }
    }

    func recordUpload(index: Int, hash: String) {
        var indices = uploadedIndices
        if !indices.contains(index) {
            indices.append(index)
            uploadedIndices = Array(indices.suffix(500))
        }
        var hashes = uploadedHashes
        if !hashes.contains(hash) {
            hashes.append(hash)
            uploadedHashes = Array(hashes.suffix(500))
        }
        uploadedCount += 1
    }
}

// MARK: - Keychain

enum KeychainError: LocalizedError {
    case status(OSStatus)

    var errorDescription: String? {
        switch self {
        case .status(let s): return "keychain error \(s)"
        }
    }
}

/// The 16-byte camera secret lives here, one generic-password item per camera id.
enum Keychain {
    private static let service = "me.amees.littlecamera.secret"

    private static func baseQuery(for cameraId: String) -> [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: cameraId,
        ]
    }

    static func secret(for cameraId: String) -> Data? {
        var query = baseQuery(for: cameraId)
        query[kSecReturnData as String] = true
        query[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &item)
        guard status == errSecSuccess else { return nil }
        return item as? Data
    }

    static func store(secret: Data, for cameraId: String) throws {
        // Replace rather than update: simpler, and the item is tiny.
        SecItemDelete(baseQuery(for: cameraId) as CFDictionary)
        var attrs = baseQuery(for: cameraId)
        attrs[kSecValueData as String] = secret
        // AfterFirstUnlock is the weakest class a background sync can still read after a reboot.
        attrs[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock
        let status = SecItemAdd(attrs as CFDictionary, nil)
        guard status == errSecSuccess else { throw KeychainError.status(status) }
    }

    static func delete(for cameraId: String) {
        SecItemDelete(baseQuery(for: cameraId) as CFDictionary)
    }

    /// Every secret this app holds, for the launch that finds it remembers no camera at all.
    static func deleteEverySecret() {
        SecItemDelete([
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
        ] as CFDictionary)
    }
}
