import Foundation
import UIKit
import CryptoKit
import CoreGraphics

enum SyncError: LocalizedError {
    /// The notification stream ended (disconnect) before an END frame. Carries
    /// whatever payload arrived so a GET can resume from that offset.
    case streamEnded(received: Data)
    case timeout
    case sequenceGap(expected: UInt16, got: UInt16)
    case wrongKind
    case status(String)
    case lengthMismatch(Int, UInt32)
    case crcMismatch

    var errorDescription: String? {
        switch self {
        case .streamEnded(let d): return "disconnected after \(d.count) bytes"
        case .timeout: return "camera stopped answering"
        case .sequenceGap(let e, let g): return "lost a frame (expected \(e), got \(g))"
        case .wrongKind: return "unexpected frame kind"
        case .status(let s): return "camera said: \(s)"
        case .lengthMismatch(let got, let want): return "got \(got) bytes, camera counted \(want)"
        case .crcMismatch: return "crc mismatch"
        }
    }
}

/// Keeps an iOS background task open for as long as the object lives or `end()` is called.
@MainActor
final class BackgroundActivity {
    private var id: UIBackgroundTaskIdentifier = .invalid

    init(name: String) {
        id = UIApplication.shared.beginBackgroundTask(withName: name) { [weak self] in
            Task { @MainActor in
                self?.end()
            }
        }
    }

    func end() {
        guard id != .invalid else { return }
        UIApplication.shared.endBackgroundTask(id)
        id = .invalid
    }
}

/// The bridge algorithm, protocol §3.5. One instance per app; `run` is a full
/// pass over the camera's unsynced photos and is safe to call again afterwards.
@MainActor
final class SyncEngine {
    enum Event {
        case progress(done: Int, total: Int)
        case uploaded(index: UInt16, image: CGImage?, kind: String)
        case finished(uploaded: Int, info: CameraInfo)
        case failed(String)
    }

    struct Dating {
        let epoch: UInt32?
        let source: String?
    }

    /// Seconds of silence on the Data characteristic before a stream is abandoned.
    /// The camera answers a write within one main-loop iteration (~30 ms) and a 9.6 KB
    /// file takes well under a second, so this only fires when something is wrong.
    static let frameTimeout: TimeInterval = 8
    /// New photos can land while a pass runs; a couple of extra passes cover that.
    static let maxPasses = 3

    var onEvent: (@MainActor (Event) -> Void)?
    private(set) var isRunning = false

    private let link: CameraLink
    private let server: ServerClient
    private let settings: Settings
    private let log: SyncLog
    /// Bytes received for files cut off by a disconnect, by index. In memory only:
    /// after a background relaunch the GET simply starts from offset 0 again.
    private var partial: [UInt16: Data] = [:]
    private var lastFrameAt = Date()
    private var timedOut = false

    init(link: CameraLink, server: ServerClient, settings: Settings, log: SyncLog) {
        self.link = link
        self.server = server
        self.settings = settings
        self.log = log
    }

    /// `info` is the Info read right after connecting.
    func run(info initial: CameraInfo) async {
        guard !isRunning else { return }
        isRunning = true
        // The phone usually goes back into a pocket right after the shutter press;
        // the ~30 s iOS grants here is enough for a handful of photos.
        let activity = BackgroundActivity(name: "me.amees.littlecamera.sync")
        defer {
            activity.end()
            isRunning = false
        }
        var info = initial
        var uploadedTotal = 0
        do {
            var passes = 0
            repeat {
                passes += 1
                let (uploaded, after) = try await pass(info: info)
                uploadedTotal += uploaded
                info = after
                if uploaded == 0 { break }
            } while info.unsyncedCount > 0 && passes < SyncEngine.maxPasses
            settings.lastSyncAt = Date()
            log.add("sync done: \(uploadedTotal) uploaded, \(info.unsyncedCount) left on camera")
            onEvent?(.finished(uploaded: uploadedTotal, info: info))
        } catch is CancellationError {
            log.add("sync cancelled")
        } catch {
            log.add("sync failed: \(error.localizedDescription)")
            onEvent?(.failed(error.localizedDescription))
        }
    }

    // MARK: One pass

    private func pass(info: CameraInfo) async throws -> (uploaded: Int, info: CameraInfo) {
        let infoReadAt = Date()
        if info.busy {
            // A stream from a previous, interrupted session is still running.
            log.add("camera busy, aborting its stream")
            try await link.write(.abort)
            try await Task.sleep(nanoseconds: 100_000_000)
        }

        let now = UInt32(Date().timeIntervalSince1970)
        let tz = Int16(clamping: TimeZone.current.secondsFromGMT() / 60)
        try await link.write(.setTime(epoch: now, utcOffsetMin: tz))
        log.add("SET_TIME \(now) tz=\(tz)")

        let listPayload = try await stream(.list(from: 1, unsyncedOnly: true), expecting: .listData)
        let entries = try ListEntry.parseAll(listPayload)
            .filter { !$0.synced }
            .sorted { $0.index < $1.index }
        log.add("LIST: \(entries.count) unsynced")
        onEvent?(.progress(done: 0, total: entries.count))

        var uploaded = 0
        for (n, entry) in entries.enumerated() {
            try Task.checkCancellation()
            let file: Data
            do {
                file = try await fetch(entry)
            } catch SyncError.status(let s) {
                // Gone from the camera between LIST and GET (ring overflow, trash). Not ours to ACK.
                log.add("#\(entry.index): \(s), skipping")
                continue
            }
            let header: PBMHeader
            do {
                header = try PBM.parse(file)
            } catch {
                // Never ACK something the server cannot store; leave it for a human.
                log.add("#\(entry.index) is not a PBM (\(error.localizedDescription)), skipping")
                continue
            }
            let hash = Data(SHA256.hash(data: file)).hexString
            let dating = SyncEngine.date(header: header, entry: entry, info: info, infoReadAt: infoReadAt)
            var kind = "photo"
            if settings.uploadedHashes.contains(hash) {
                // The server confirmed this file earlier but the ACK never reached
                // the camera (disconnect in between). Just ACK.
                log.add("#\(entry.index) already on the server")
            } else {
                let outcome = try await server.uploadPhoto(
                    pbm: file,
                    index: entry.index,
                    capturedAt: dating.epoch,
                    source: dating.source
                )
                settings.recordUpload(index: Int(entry.index), hash: hash)
                kind = outcome.kind
                log.add("#\(entry.index) → \(outcome.statusCode) \(outcome.isDuplicate ? "duplicate" : kind)")
            }
            try await link.write(.ack(index: entry.index))
            partial[entry.index] = nil
            uploaded += 1
            onEvent?(.uploaded(index: entry.index, image: PBM.image(file), kind: kind))
            onEvent?(.progress(done: n + 1, total: entries.count))
        }

        let after = try await link.readInfo()
        return (uploaded, after)
    }

    /// GET with resume: continue from the bytes already held for this index.
    private func fetch(_ entry: ListEntry) async throws -> Data {
        var have = partial[entry.index] ?? Data()
        let offset = UInt32(have.count)
        if offset > 0 {
            log.add("#\(entry.index): resuming at \(offset)")
        }
        do {
            let rest = try await stream(.get(index: entry.index, offset: offset), expecting: .photoData)
            have.append(rest)
            return have
        } catch SyncError.streamEnded(let received) {
            have.append(received)
            partial[entry.index] = have
            throw SyncError.streamEnded(received: received)
        } catch SyncError.crcMismatch {
            // Do not build on corrupt bytes; next time start from zero.
            partial[entry.index] = nil
            throw SyncError.crcMismatch
        }
    }

    /// Writes `op`, then collects the Data frames it produces up to END and
    /// verifies status, sequence continuity, length and CRC-32.
    private func stream(_ op: ControlOp, expecting kind: FrameKind) async throws -> Data {
        // Open the stream before the write: the first frame can arrive ~30 ms later.
        let frames = link.frames()
        try await link.write(op)

        lastFrameAt = Date()
        timedOut = false
        // Watchdog: a camera that dies mid-stream is only reported by iOS after the
        // supervision timeout; close the stream ourselves if nothing arrives.
        let watchdog = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                guard let self = self, !Task.isCancelled else { return }
                if Date().timeIntervalSince(self.lastFrameAt) > SyncEngine.frameTimeout {
                    self.timedOut = true
                    self.link.closeFrames()
                    return
                }
            }
        }
        defer { watchdog.cancel() }

        var payload = Data()
        var expectedSeq: UInt16 = 0
        var end: EndFrame?
        for await frame in frames {
            lastFrameAt = Date()
            guard frame.seq == expectedSeq else {
                try? await link.write(.abort)
                throw SyncError.sequenceGap(expected: expectedSeq, got: frame.seq)
            }
            expectedSeq &+= 1
            if frame.kind == .end {
                end = try EndFrame(payload: frame.payload)
                break
            }
            guard frame.kind == kind else {
                try? await link.write(.abort)
                throw SyncError.wrongKind
            }
            payload.append(frame.payload)
        }

        guard let endFrame = end else {
            if timedOut {
                try? await link.write(.abort)
                throw SyncError.timeout
            }
            throw SyncError.streamEnded(received: payload)
        }
        guard endFrame.status == EndStatus.ok.rawValue else {
            throw SyncError.status(endFrame.statusLabel)
        }
        guard payload.count == Int(endFrame.totalLength) else {
            throw SyncError.lengthMismatch(payload.count, endFrame.totalLength)
        }
        guard CRC32.checksum(payload) == endFrame.crc32 else {
            throw SyncError.crcMismatch
        }
        return payload
    }

    // MARK: Dating (protocol §2)

    /// What to put in `X-Captured-At` / `X-Captured-At-Source`. Nil means the
    /// server can work it out itself (the file carries `t=`, or it is upload time).
    static func date(header: PBMHeader, entry: ListEntry, info: CameraInfo, infoReadAt: Date) -> Dating {
        // 1. The camera knew the time: the file says so and the server reads it.
        if let t = header.t, t > 0 {
            return Dating(epoch: nil, source: nil)
        }
        // 2. Same boot as now: count back from the camera's uptime at the Info read.
        let up = header.up ?? entry.uptimeMs
        if let boot = header.boot, boot == info.boot, info.uptimeMs >= up {
            let captured = infoReadAt.timeIntervalSince1970 - Double(info.uptimeMs - up) / 1000
            return Dating(epoch: UInt32(max(0, captured)), source: "phone")
        }
        // Old files have no comment line; the camera may still have an estimate in LIST.
        if header.boot == nil, entry.epoch > 0 {
            return Dating(epoch: entry.epoch, source: "phone")
        }
        // 3. Upload time, assigned server-side.
        return Dating(epoch: nil, source: nil)
    }
}
