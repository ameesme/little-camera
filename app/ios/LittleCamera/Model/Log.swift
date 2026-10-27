import Foundation

/// The on-screen sync log: the last 20 lines, newest last. Anything may post
/// from any thread; the list itself is only touched on the main actor.
@Observable @MainActor
final class SyncLog {
    static let capacity = 20

    private(set) var lines: [String] = []

    private static let clock: DateFormatter = {
        let f = DateFormatter()
        f.dateFormat = "HH:mm:ss"
        return f
    }()

    func add(_ line: String) {
        lines.append(SyncLog.clock.string(from: Date()) + "  " + line)
        if lines.count > SyncLog.capacity {
            lines.removeFirst(lines.count - SyncLog.capacity)
        }
    }

    nonisolated func post(_ line: String) {
        Task { @MainActor in
            self.add(line)
        }
    }
}
