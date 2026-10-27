import Foundation
import UIKit
import CryptoKit

/// Pushing a firmware image to the camera (§3.7). It lands in the slot the camera is not running from, so nothing here can brick it.
@MainActor
final class FirmwareUpdater {
    enum Phase: Equatable {
        case idle
        case downloading
        /// Bytes the camera has accepted, out of the image size.
        case sending(sent: Int, total: Int)
        case verifying
        case restarting
        case failed(String)

        var isRunning: Bool {
            switch self {
            case .downloading, .sending, .verifying, .restarting: return true
            case .idle, .failed: return false
            }
        }
    }

    enum UpdateError: LocalizedError {
        case cameraCannotUpdate
        case cameraBusy
        case refused(String)
        case silent
        case linkLost

        var errorDescription: String? {
            switch self {
            case .cameraCannotUpdate: return "this camera cannot take an update over Bluetooth"
            case .cameraBusy: return "the camera is busy with something else"
            case .refused(let why): return "the camera refused the image: \(why)"
            case .silent: return "the camera stopped answering"
            case .linkLost: return "the camera disconnected"
            }
        }
    }

    /// The camera answers a window inside one loop iteration, so this only fires once it has stopped.
    static let statusTimeout: TimeInterval = 15
    /// The final verify reads the whole slot back off flash, which is seconds rather than milliseconds.
    static let verifyTimeout: TimeInterval = 60

    var onPhase: (@MainActor (Phase) -> Void)?
    private(set) var phase: Phase = .idle {
        didSet { if phase != oldValue { onPhase?(phase) } }
    }
    /// A run is in flight, unlike `phase.isRunning`, which stays `.restarting` while the camera reboots.
    private(set) var isRunning = false

    /// The camera is back, so whichever way the last push ended, the card goes back to reporting what it runs now.
    func settle() {
        guard !isRunning else { return }
        phase = .idle
    }

    private let link: CameraLink
    private let server: ServerClient
    private let log: SyncLog

    // Set by the frame consumer, read by the sender.
    private var acked: UInt32 = 0
    private var chunk = 0
    private var window = 0
    private var finalStatus: UpdateStatus?
    private var failure: Error?
    private var rewindTo: UInt32?
    private var sent: UInt32 = 0
    private var waiter: CheckedContinuation<Void, Never>?
    /// The sender waits on this count, not on `acked` moving: UPDATE_BEGIN answers with an offset that is usually unchanged.
    private var statusCount = 0

    init(link: CameraLink, server: ServerClient, log: SyncLog) {
        self.link = link
        self.server = server
        self.log = log
    }

    /// Download `release` and push it. Returns true when the camera has the image and is rebooting into it.
    @discardableResult
    func run(release: FirmwareRelease, info: CameraInfo) async -> Bool {
        guard !isRunning else { return false }
        isRunning = true
        defer { isRunning = false }
        // Minutes of writing against a ~30 s background window, so hold the screen awake and ask the owner to stay put.
        let activity = BackgroundActivity(name: "me.amees.littlecamera.firmware")
        UIApplication.shared.isIdleTimerDisabled = true
        defer {
            UIApplication.shared.isIdleTimerDisabled = false
            activity.end()
        }
        do {
            try await push(release: release, info: info)
            phase = .restarting
            log.add("firmware \(release.version) sent; the camera is restarting into it")
            return true
        } catch {
            // Whatever happened, the camera is still running what it was.
            phase = .failed(error.localizedDescription)
            log.add("firmware update failed: \(error.localizedDescription)")
            // A quiet or dropped camera keeps its half-written slot for 90 s and can resume, so only tell the others to let go.
            switch error as? UpdateError {
            case .silent, .linkLost: break
            default: try? await link.write(.updateAbort)
            }
            return false
        }
    }

    private func push(release: FirmwareRelease, info: CameraInfo) async throws {
        guard link.canUpdate, info.firmware != nil else { throw UpdateError.cameraCannotUpdate }
        guard info.canTake(imageOf: release.size) else { throw UpdateError.cameraCannotUpdate }
        guard !info.busy else { throw UpdateError.cameraBusy }

        phase = .downloading
        log.add("downloading firmware \(release.version) (\(release.size) bytes)")
        let image = try await server.downloadFirmware(release)
        let digest = Data(SHA256.hash(data: image))

        // Open the stream before the write, as everywhere else: the answer can arrive a loop iteration later.
        let frames = link.frames()
        defer { link.closeFrames() }
        reset()
        let consumer = consume(frames)
        defer { consumer.cancel() }

        try await link.write(.updateBegin(size: UInt32(image.count), sha256: digest))
        try await waitForStatus(timeout: FirmwareUpdater.statusTimeout)
        if let failure = failure { throw failure }
        guard chunk > 0, window > 0 else { throw UpdateError.refused("no window") }
        if acked > 0 { log.add("camera still had \(acked) bytes from an earlier try; resuming") }

        // Never ask the camera for more than this phone's link can carry.
        let maxPayload = link.maxChunkPayload
        let size = UInt32(image.count)
        let step = min(chunk, maxPayload > 0 ? maxPayload : chunk)
        sent = acked

        // An UPDATE_END answered with an offset means a gap right at the end: worth filling, but not forever.
        for attempt in 0..<3 {
            try await sendAll(image: image, size: size, step: step)

            phase = .verifying
            log.add("all \(image.count) bytes accepted; verifying on the camera")
            finalStatus = nil
            try await link.write(.updateEnd)
            try await waitForFinal(timeout: FirmwareUpdater.verifyTimeout)
            if let failure = failure { throw failure }
            guard let final = finalStatus else { throw UpdateError.silent }
            if final.isOK { return }
            guard final.needsRewind, attempt < 2 else { throw UpdateError.refused(final.statusLabel) }
            log.add("camera was still short at \(final.nextOffset); filling the gap")
            rewindTo = final.nextOffset
            finalStatus = nil
            phase = .sending(sent: Int(final.nextOffset), total: image.count)
        }
        throw UpdateError.refused("the camera kept losing chunks")
    }

    /// Push chunks until the camera has accepted the whole image.
    private func sendAll(image: Data, size: UInt32, step: Int) async throws {
        phase = .sending(sent: Int(acked), total: Int(size))
        while acked < size {
            if let failure = failure { throw failure }
            if let to = rewindTo {
                rewindTo = nil
                // A chunk went missing. Everything after it is ahead of the camera too, so go back to where it actually is.
                if to != sent { log.add("rewound to \(to) (was at \(sent))") }
                sent = to
            }
            // `acked` moves under these awaits, so recompute what is outstanding rather than counting it off.
            while sent < size && rewindTo == nil && failure == nil {
                // Unsigned: a camera claiming more than was sent would trap here rather than simply be ignored.
                let outstanding = sent > acked ? Int(sent - acked) : 0
                if outstanding >= window { break }
                let end = min(sent + UInt32(step), size)
                let slice = image.subdata(in: Int(sent)..<Int(end))
                try await link.sendUpdateChunk(offset: sent, data: slice)
                sent = end
            }
            if acked >= size { return }
            // Window full or image sent: either way, nothing to do until the camera says how much landed.
            try await waitForStatus(timeout: FirmwareUpdater.statusTimeout)
            phase = .sending(sent: Int(acked), total: Int(size))
        }
    }

    // MARK: Frames

    private func reset() {
        acked = 0
        sent = 0
        chunk = 0
        window = 0
        statusCount = 0
        finalStatus = nil
        failure = nil
        rewindTo = nil
    }

    private func consume(_ frames: AsyncStream<DataFrame>) -> Task<Void, Never> {
        Task { @MainActor [weak self] in
            for await frame in frames {
                guard let self = self else { return }
                guard frame.kind == .updateStatus,
                      let status = try? UpdateStatus(payload: frame.payload) else { continue }
                self.apply(status)
                self.wake()
            }
            // The stream only ends on a disconnect or a new stream taking over.
            guard let self = self else { return }
            if self.failure == nil && self.finalStatus == nil {
                self.failure = UpdateError.linkLost
            }
            self.wake()
        }
    }

    private func apply(_ status: UpdateStatus) {
        statusCount += 1
        chunk = max(chunk, Int(status.chunk))
        window = max(window, Int(status.window))
        acked = status.nextOffset
        if status.op == ControlOp.updateEnd.code || status.state == .ready || status.state == .failed {
            finalStatus = status
        }
        if status.needsRewind {
            rewindTo = status.nextOffset
        } else if !status.isOK {
            failure = UpdateError.refused(status.statusLabel)
        }
    }

    /// One waiter at a time, because only the sender waits and only when it has nothing left to send.
    private func waitForStatus(timeout: TimeInterval) async throws {
        let before = statusCount
        let deadline = Date().addingTimeInterval(timeout)
        while failure == nil && finalStatus == nil && statusCount == before {
            if Date() >= deadline { throw UpdateError.silent }
            await withCheckedContinuation { (cont: CheckedContinuation<Void, Never>) in
                waiter = cont
                // A timer wakes the wait so the deadline above is what decides; a spurious wake just re-checks the condition.
                Task { @MainActor in
                    try? await Task.sleep(nanoseconds: 500_000_000)
                    self.wake()
                }
            }
        }
    }

    private func waitForFinal(timeout: TimeInterval) async throws {
        let deadline = Date().addingTimeInterval(timeout)
        while failure == nil && finalStatus == nil {
            if Date() >= deadline { throw UpdateError.silent }
            await withCheckedContinuation { (cont: CheckedContinuation<Void, Never>) in
                waiter = cont
                Task { @MainActor in
                    try? await Task.sleep(nanoseconds: 500_000_000)
                    self.wake()
                }
            }
        }
    }

    private func wake() {
        guard let cont = waiter else { return }
        waiter = nil
        cont.resume()
    }
}
