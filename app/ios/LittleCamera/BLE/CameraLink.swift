import Foundation
@preconcurrency import CoreBluetooth

enum LinkError: LocalizedError {
    case notConnected
    case busy
    case disconnected
    case noValue
    case badSecretLength(Int)
    case noUpdateCharacteristic

    var errorDescription: String? {
        switch self {
        case .notConnected: return "camera not connected"
        case .busy: return "another BLE operation is in flight"
        case .disconnected: return "camera disconnected"
        case .noValue: return "characteristic had no value"
        case .badSecretLength(let n): return "secret is \(n) bytes, expected 16"
        case .noUpdateCharacteristic: return "this camera's firmware cannot be updated over Bluetooth"
        }
    }
}

/// One camera, one connection. Every stored property is touched only on `queue`, which is what `@unchecked Sendable` stands in for.
final class CameraLink: NSObject, @unchecked Sendable {
    enum Event {
        case bluetooth(CBManagerState)
        case scanning
        case found(id: UUID, name: String)
        case connecting(name: String)
        /// Service discovered, notifications on Data enabled: safe to read/write.
        case ready(name: String)
        case disconnected(String?)
        case log(String)
    }

    private static let restoreIdentifier = "me.amees.littlecamera.central"

    private let events: @MainActor (Event) -> Void
    private let queue = DispatchQueue(label: "me.amees.littlecamera.ble")
    private var central: CBCentralManager?
    private var pairedId: UUID?
    private var peripheral: CBPeripheral?
    /// CoreBluetooth drops a CBPeripheral nobody retains, so these are held until the owner picks one.
    private var found: [UUID: CBPeripheral] = [:]
    private var characteristics: [CBUUID: CBCharacteristic] = [:]
    private var pendingReads: [CBUUID: CheckedContinuation<Data, Error>] = [:]
    private var pendingWrite: CheckedContinuation<Void, Error>?
    private var frameContinuation: AsyncStream<DataFrame>.Continuation?
    /// A chunk waiting for the controller to drain, with the caller it lets go of once the bytes are on the wire.
    private var queuedChunk: (Data, CheckedContinuation<Void, Error>)?

    init(pairedId: UUID?, events: @escaping @MainActor (Event) -> Void) {
        self.pairedId = pairedId
        self.events = events
        super.init()
    }

    /// As early after launch as possible: iOS expects the manager with our restore identifier to already exist.
    func start() {
        queue.async {
            guard self.central == nil else { return }
            self.central = CBCentralManager(
                delegate: self,
                queue: self.queue,
                options: [
                    CBCentralManagerOptionRestoreIdentifierKey: CameraLink.restoreIdentifier,
                    CBCentralManagerOptionShowPowerAlertKey: true,
                ]
            )
        }
    }

    /// The owner chose a camera in SetupView. Stops scanning and connects to it.
    func use(peripheralId: UUID) {
        queue.async {
            self.pairedId = peripheralId
            self.central?.stopScan()
            let known = self.found[peripheralId]
                ?? self.central?.retrievePeripherals(withIdentifiers: [peripheralId]).first
            if let p = known {
                self.connect(p)
            } else {
                self.emit(.log("peripheral \(peripheralId.uuidString) not found, scanning"))
                self.scan()
            }
        }
    }

    /// Drops the connection and forgets which camera this was, so scanning starts over.
    func forget() {
        queue.async {
            self.pairedId = nil
            if let p = self.peripheral { self.central?.cancelPeripheralConnection(p) }
            self.peripheral = nil
            self.characteristics = [:]
            self.found = [:]
            self.scan()
        }
    }

    var isReady: Bool {
        queue.sync {
            peripheral?.state == .connected && characteristics[LC.dataUUID]?.isNotifying == true
        }
    }

    // MARK: Async API

    func readInfo() async throws -> CameraInfo {
        try CameraInfo(data: try await read(LC.infoUUID))
    }

    /// Encrypted and authenticated, so this read is what makes iOS show the passkey prompt. Later reads are silent.
    func readSecret() async throws -> Data {
        let d = try await read(LC.secretUUID)
        guard d.count == 16 else { throw LinkError.badSecretLength(d.count) }
        return d
    }

    /// Write with response. Only one write in flight at a time; the camera serialises anyway.
    func write(_ op: ControlOp) async throws {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            self.queue.async {
                guard let p = self.peripheral, p.state == .connected,
                      let c = self.characteristics[LC.controlUUID] else {
                    cont.resume(throwing: LinkError.notConnected)
                    return
                }
                guard self.pendingWrite == nil else {
                    cont.resume(throwing: LinkError.busy)
                    return
                }
                self.pendingWrite = cont
                p.writeValue(op.encoded, for: c, type: .withResponse)
            }
        }
    }

    /// Opened *before* the write that starts a stream, and opening one finishes the last, so leftovers cannot leak.
    func frames() -> AsyncStream<DataFrame> {
        let (stream, continuation) = AsyncStream.makeStream(of: DataFrame.self, bufferingPolicy: .unbounded)
        queue.sync {
            self.frameContinuation?.finish()
            self.frameContinuation = continuation
        }
        return stream
    }

    func closeFrames() {
        queue.async {
            self.frameContinuation?.finish()
            self.frameContinuation = nil
        }
    }

    /// The negotiated write size less the 4-byte offset every chunk carries.
    var maxChunkPayload: Int {
        queue.sync {
            guard let p = peripheral else { return 0 }
            return max(0, p.maximumWriteValueLength(for: .withoutResponse) - 4)
        }
    }

    var canUpdate: Bool {
        queue.sync { characteristics[LC.updateUUID] != nil }
    }

    /// `[u32 offset][data]` without response (§3.7). It returns once the chunk is on its way, which the offset makes safe.
    func sendUpdateChunk(offset: UInt32, data: Data) async throws {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Void, Error>) in
            self.queue.async {
                guard let p = self.peripheral, p.state == .connected else {
                    cont.resume(throwing: LinkError.notConnected)
                    return
                }
                guard let c = self.characteristics[LC.updateUUID] else {
                    cont.resume(throwing: LinkError.noUpdateCharacteristic)
                    return
                }
                var frame = Data()
                frame.lcAppend(offset)
                frame.append(data)
                if p.canSendWriteWithoutResponse {
                    p.writeValue(frame, for: c, type: .withoutResponse)
                    cont.resume()
                    return
                }
                // Buffers are full. One parked chunk is enough, because the caller sends them one at a time.
                guard self.queuedChunk == nil else {
                    cont.resume(throwing: LinkError.busy)
                    return
                }
                self.queuedChunk = (frame, cont)
            }
        }
    }

    // MARK: Internals (queue only)

    private func emit(_ event: Event) {
        let events = self.events
        Task { @MainActor in
            events(event)
        }
    }

    private func read(_ uuid: CBUUID) async throws -> Data {
        try await withCheckedThrowingContinuation { (cont: CheckedContinuation<Data, Error>) in
            self.queue.async {
                guard let p = self.peripheral, p.state == .connected,
                      let c = self.characteristics[uuid] else {
                    cont.resume(throwing: LinkError.notConnected)
                    return
                }
                guard self.pendingReads[uuid] == nil else {
                    cont.resume(throwing: LinkError.busy)
                    return
                }
                self.pendingReads[uuid] = cont
                p.readValue(for: c)
            }
        }
    }

    private func scan() {
        guard let central = central, central.state == .poweredOn else { return }
        // Filtering on the service UUID is what makes background scans match too.
        central.scanForPeripherals(withServices: [LC.serviceUUID], options: nil)
        emit(.scanning)
    }

    private func connect(_ p: CBPeripheral) {
        p.delegate = self
        peripheral = p
        emit(.connecting(name: p.name ?? "camera"))
        central?.connect(p, options: nil)
    }

    private func discover(_ p: CBPeripheral) {
        characteristics = [:]
        p.discoverServices([LC.serviceUUID])
    }

    private func failPending(_ error: Error) {
        let reads = pendingReads
        pendingReads = [:]
        for (_, cont) in reads {
            cont.resume(throwing: error)
        }
        if let w = pendingWrite {
            pendingWrite = nil
            w.resume(throwing: error)
        }
        // A parked chunk is waiting on a callback that is never coming now.
        if let (_, cont) = queuedChunk {
            queuedChunk = nil
            cont.resume(throwing: error)
        }
        frameContinuation?.finish()
        frameContinuation = nil
    }
}

// MARK: - CBCentralManagerDelegate

extension CameraLink: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        emit(.bluetooth(central.state))
        guard central.state == .poweredOn else {
            characteristics = [:]
            failPending(LinkError.disconnected)
            return
        }
        if let p = peripheral {
            // Restored, or Bluetooth was toggled: carry on if iOS kept the link, otherwise queue a connect.
            if p.state == .connected {
                discover(p)
            } else {
                connect(p)
            }
            return
        }
        if let id = pairedId, let p = central.retrievePeripherals(withIdentifiers: [id]).first {
            connect(p)
        } else {
            scan()
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        // Relaunched for a pending connect: adopt the peripheral, and didUpdateState decides what to do with it.
        if let restored = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
           let p = restored.first {
            p.delegate = self
            peripheral = p
            emit(.log("restored \(p.name ?? "peripheral"), state \(p.state.rawValue)"))
        }
    }

    func centralManager(_ central: CBCentralManager,
                        didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any],
                        rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String)
            ?? peripheral.name
            ?? "lc-????"
        found[peripheral.identifier] = peripheral
        if peripheral.identifier == pairedId {
            central.stopScan()
            connect(peripheral)
            return
        }
        emit(.found(id: peripheral.identifier, name: name))
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        // Logged for the MTU alone, which is the withoutResponse write length plus 3.
        let mtu = peripheral.maximumWriteValueLength(for: .withoutResponse) + 3
        emit(.log("connected \(peripheral.name ?? "camera"), MTU \(mtu)"))
        discover(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        emit(.log("connect failed: \(error?.localizedDescription ?? "unknown")"))
        // Rare, since pending connects usually just wait. Back off and re-arm.
        queue.asyncAfter(deadline: .now() + 2) {
            if self.peripheral?.identifier == peripheral.identifier {
                self.connect(peripheral)
            }
        }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        characteristics = [:]
        failPending(LinkError.disconnected)
        emit(.disconnected(error?.localizedDescription))
        // The camera light-sleeps and takes its radio with it, so this ends every session; re-arm for the next wake.
        if peripheral.identifier == self.peripheral?.identifier {
            connect(peripheral)
        }
    }
}

// MARK: - CBPeripheralDelegate

extension CameraLink: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard error == nil,
              let service = peripheral.services?.first(where: { $0.uuid == LC.serviceUUID }) else {
            emit(.log("service missing: \(error?.localizedDescription ?? "not advertised")"))
            central?.cancelPeripheralConnection(peripheral)
            return
        }
        peripheral.discoverCharacteristics(LC.characteristicUUIDs, for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            emit(.log("characteristics: \(error.localizedDescription)"))
            return
        }
        for c in service.characteristics ?? [] {
            characteristics[c.uuid] = c
        }
        guard let data = characteristics[LC.dataUUID] else {
            emit(.log("Data characteristic missing"))
            return
        }
        peripheral.setNotifyValue(true, for: data)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == LC.dataUUID else { return }
        if let error = error {
            emit(.log("notify: \(error.localizedDescription)"))
            return
        }
        if characteristic.isNotifying {
            emit(.ready(name: peripheral.name ?? "camera"))
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.uuid == LC.dataUUID {
            if let value = characteristic.value, let frame = DataFrame(data: value) {
                frameContinuation?.yield(frame)
            } else {
                emit(.log("dropped a malformed frame"))
            }
            return
        }
        guard let cont = pendingReads.removeValue(forKey: characteristic.uuid) else { return }
        if let error = error {
            cont.resume(throwing: error)
        } else if let value = characteristic.value {
            cont.resume(returning: value)
        } else {
            cont.resume(throwing: LinkError.noValue)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let cont = pendingWrite else { return }
        pendingWrite = nil
        if let error = error {
            cont.resume(throwing: error)
        } else {
            cont.resume()
        }
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        guard let (frame, cont) = queuedChunk, let c = characteristics[LC.updateUUID] else { return }
        queuedChunk = nil
        peripheral.writeValue(frame, for: c, type: .withoutResponse)
        cont.resume()
    }

    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        // Firmware update or a reboot with a different table: start over.
        if invalidatedServices.contains(where: { $0.uuid == LC.serviceUUID }) {
            failPending(LinkError.disconnected)
            discover(peripheral)
        }
    }
}
