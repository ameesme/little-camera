import Foundation
import UIKit
import CryptoKit

/// Pushing a firmware image to the camera, protocol §3.7.
///
/// The phone is still a pipe here: it fetches an image the server published,
/// checks it, and writes it to the camera in chunks that each carry their own
/// offset. It is not trusted with any of it — the camera checks the whole
/// image against the SHA-256 that went out in `UPDATE_BEGIN` before it points
/// the bootloader at anything, and writes it to the slot it is not running
/// from, so nothing this class can get wrong costs the owner their camera.
///
/// Two kinds of flow control are in play at once and they are not the same
/// thing. CoreBluetooth's `canSendWriteWithoutResponse` says whether *this
/// phone's* controller has room; the camera's window says how much it can
/// stage while its main loop writes flash. Keeping to the window is what makes
/// a dropped chunk a rarity rather than the normal case, and an offset that
/// came back wrong is what makes it survivable either way.
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

    /// Silence on the Data characteristic that means something is wrong. The
    /// camera answers a window inside a loop iteration (~30 ms) and reports
    /// progress every half window, so this only fires when it has stopped.
    static let statusTimeout: TimeInterval = 15
    /// The verify at the end reads the whole slot back off flash before the
    /// camera answers UPDATE_END, which is seconds rather than milliseconds.
    static let verifyTimeout: TimeInterval = 60

    var onPhase: (@MainActor (Phase) -> Void)?
    private(set) var phase: Phase = .idle {
        didSet { if phase != oldValue { onPhase?(phase) } }
    }
    /// A run() is in flight. Distinct from `phase.isRunning`, which stays at
    /// `.restarting` after a successful push while the camera reboots.
    private(set) var isRunning = false

    /// The camera is back. Whatever the last update ended in — a restart into
    /// the new firmware, or a failure that left it on the old one — the card
    /// goes back to reporting what it runs now.
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
    /// Bumped by every UPDATE_STATUS. The sender waits on this rather than on
    /// `acked` moving: the answer to UPDATE_BEGIN carries the offset the camera
    /// is already at, which is usually the offset it was at before, and waiting
    /// for that number to change would be waiting for a frame that never comes.
    private var statusCount = 0

    init(link: CameraLink, server: ServerClient, log: SyncLog) {
        self.link = link
        self.server = server
        self.log = log
    }

    /// Download `release` and push it. Returns true when the camera has the
    /// image and is rebooting into it.
    @discardableResult
    func run(release: FirmwareRelease, info: CameraInfo) async -> Bool {
        guard !isRunning else { return false }
        isRunning = true
        defer { isRunning = false }
        // Minutes of Bluetooth writing. The background task window is ~30 s,
        // which is enough for a few photos and not for a megabyte of firmware,
        // so hold the screen awake as well and tell the owner to stay put —
        // the card says so too. Worst case the session is interrupted and the
        // camera keeps what it was running.
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
            // A camera that went quiet or dropped the link holds its
            // half-written slot for a minute and a half, and a retry inside
            // that window carries on from where this one stopped (§3.7). So
            // don't throw it away. Anything else is over as far as the camera
            // is concerned: say so, and let it free the flash handle now
            // rather than on its own timeout.
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

        // Open the stream before the write, as everywhere else: the answer can
        // arrive a loop iteration later.
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

        // Send, then finish. An UPDATE_END the camera answers with "offset"
        // means it has less than both sides thought — a gap right at the end.
        // That is worth going back for rather than throwing a whole image
        // away, but not forever: a camera that keeps losing the same chunk is
        // telling us something else is wrong.
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
                // A chunk went missing. Everything after it is ahead of the
                // camera too, so go back to where it actually is.
                if to != sent { log.add("rewound to \(to) (was at \(sent))") }
                sent = to
            }
            // Fill the window. `acked` moves under us while these awaits are
            // suspended — the consumer is on the same actor — so recompute what
            // is outstanding each time round rather than counting it off: an
            // ack that lands mid-window frees room immediately.
            while sent < size && rewindTo == nil && failure == nil {
                // Unsigned: a camera claiming more than was sent would trap
                // here rather than simply be ignored.
                let outstanding = sent > acked ? Int(sent - acked) : 0
                if outstanding >= window { break }
                let end = min(sent + UInt32(step), size)
                let slice = image.subdata(in: Int(sent)..<Int(end))
                try await link.sendUpdateChunk(offset: sent, data: slice)
                sent = end
            }
            if acked >= size { return }
            // Either the window is full or the image is out; either way there
            // is nothing to do until the camera says how much of it landed.
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

    /// Suspend until the camera says something, or give up. One waiter at a
    /// time: only the sender waits, and only when it has nothing left to send.
    private func waitForStatus(timeout: TimeInterval) async throws {
        let before = statusCount
        let deadline = Date().addingTimeInterval(timeout)
        while failure == nil && finalStatus == nil && statusCount == before {
            if Date() >= deadline { throw UpdateError.silent }
            await withCheckedContinuation { (cont: CheckedContinuation<Void, Never>) in
                waiter = cont
                // A timer resumes the wait even when nothing arrives, so the
                // deadline above is what actually decides. Being woken for no
                // reason is harmless: the condition above is re-checked.
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
