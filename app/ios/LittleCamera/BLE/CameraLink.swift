import Foundation
import CoreBluetooth

enum LinkError: LocalizedError {
    case notConnected
    case busy
    case disconnected
    case noValue
    case badSecretLength(Int)

    var errorDescription: String? {
        switch self {
        case .notConnected: return "camera not connected"
        case .busy: return "another BLE operation is in flight"
        case .disconnected: return "camera disconnected"
        case .noValue: return "characteristic had no value"
        case .badSecretLength(let n): return "secret is \(n) bytes, expected 16"
        }
    }
}

/// One camera, one connection. All CoreBluetooth state lives on `queue`; the
/// outside world gets `Event`s on the main actor and a handful of async calls.
///
/// Background strategy: once the owner has picked a camera we always keep a
/// `connect` pending. iOS does not time pending connects out, and with the
/// `bluetooth-central` background mode it relaunches us (via state restoration)
/// when the camera advertises again, even if the app was suspended or killed
/// for memory. That is the only wake path we need; there is no polling.
final class CameraLink: NSObject {
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
    /// Peripherals seen while scanning. CoreBluetooth drops a CBPeripheral we do
    /// not retain, so keep them until the owner picks one.
    private var found: [UUID: CBPeripheral] = [:]
    private var characteristics: [CBUUID: CBCharacteristic] = [:]
    private var pendingReads: [CBUUID: CheckedContinuation<Data, Error>] = [:]
    private var pendingWrite: CheckedContinuation<Void, Error>?
    private var frameContinuation: AsyncStream<DataFrame>.Continuation?

    init(pairedId: UUID?, events: @escaping @MainActor (Event) -> Void) {
        self.pairedId = pairedId
        self.events = events
        super.init()
    }

    /// Creates the central. Call once, as early as possible after launch: iOS
    /// expects the manager with the restore identifier to exist when it relaunches us.
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

    var isReady: Bool {
        queue.sync {
            peripheral?.state == .connected && characteristics[LC.dataUUID]?.isNotifying == true
        }
    }

    // MARK: Async API

    func readInfo() async throws -> CameraInfo {
        try CameraInfo(data: try await read(LC.infoUUID))
    }

    /// Reading Secret is what triggers pairing: the characteristic is encrypted +
    /// authenticated, so on first contact iOS shows the passkey prompt by itself and
    /// the read completes once the bond exists. Later reads are silent.
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

    /// A fresh stream of Data notifications. Call it *before* writing the LIST/GET
    /// that starts a stream; opening a new one finishes the previous one, so
    /// leftovers from an aborted transfer can never leak into the next.
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
            // Restored from a background relaunch, or Bluetooth was toggled: if iOS
            // kept the link alive pick up where we were, otherwise queue a connect.
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
        // iOS relaunched us in the background because of a pending connect (or an
        // open connection). Adopt the peripheral; didUpdateState follows and decides.
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
        // Not used for writes (we only notify), but it tells us the negotiated MTU:
        // withoutResponse length is ATT MTU - 3.
        let mtu = peripheral.maximumWriteValueLength(for: .withoutResponse) + 3
        emit(.log("connected \(peripheral.name ?? "camera"), MTU \(mtu)"))
        discover(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        emit(.log("connect failed: \(error?.localizedDescription ?? "unknown")"))
        // Rare (pending connects usually just wait). Back off a little and re-arm.
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
        // The camera light-sleeps and takes its radio down; this is the normal end
        // of every session. Re-arm the connect so the next wake finds us waiting.
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

    func peripheral(_ peripheral: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        // Firmware update or a reboot with a different table: start over.
        if invalidatedServices.contains(where: { $0.uuid == LC.serviceUUID }) {
            failPending(LinkError.disconnected)
            discover(peripheral)
        }
    }
}
