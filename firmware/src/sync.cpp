#include "sync.h"

#include <NimBLEDevice.h>
#include <esp_bt.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include <time.h>

#include "crc32.h"
#include "identity.h"
#include "storage.h"
#include "sync_protocol.h"

namespace Sync {

namespace {

using namespace SyncProto;

// A connected phone that has gone quiet for this long stops holding the
// camera awake. A stuck app cannot pin the battery; end() drops the link.
constexpr uint32_t BLE_KEEPAWAKE_MS = 60000;
// After a wake with photos to send, advertise this long instead of the usual
// 10s idle window. iOS background scanning can take several seconds to notice
// an advertiser, and the whole point is that the phone in the pocket gets the
// photo without being asked.
constexpr uint32_t UNSYNCED_LINGER_MS = 30000;
// Notifications pushed per loop() call. Enough to saturate the link at a
// 15-30ms connection interval, few enough that the viewfinder keeps drawing.
constexpr int CHUNKS_PER_LOOP = 8;
// How often loop() re-packs the Info characteristic. Uptime in it is at most
// this stale, which the dating rule can live with.
constexpr uint32_t INFO_REFRESH_MS = 500;
// Same plausibility bar as storage: below this the clock was never set.
constexpr time_t EPOCH_PLAUSIBLE = 1600000000;

constexpr int EVENT_QUEUE = 8;

// ---- state shared between the NimBLE task and loop() ----
// Written in callbacks, read in loop(). Single-writer per field, and the
// pending command is a one-slot mailbox guarded by its flag, so plain
// volatiles are enough.

struct PendingCommand {
    uint8_t bytes[8];
    uint8_t len;
};
volatile bool _cmdPending = false;
PendingCommand _cmd;

volatile bool _connected = false;
volatile bool _subscribed = false;
volatile uint16_t _peerMtu = 23;
volatile uint32_t _lastBleActivity = 0;

Event _events[EVENT_QUEUE];
volatile int _evHead = 0;
volatile int _evTail = 0;

void pushEvent(Event::Kind kind, uint32_t value = 0) {
    int next = (_evHead + 1) % EVENT_QUEUE;
    if (next == _evTail) return;  // Full: drop; these are toasts, not data
    _events[_evHead].kind = kind;
    _events[_evHead].value = value;
    _evHead = next;
}

// ---- loop()-only state ----

bool _running = false;
NimBLEServer* _server = nullptr;
NimBLECharacteristic* _info = nullptr;
NimBLECharacteristic* _secret = nullptr;
NimBLECharacteristic* _control = nullptr;
NimBLECharacteristic* _data = nullptr;

uint32_t _lastInfoRefresh = 0;
int _sentThisConnection = 0;
uint32_t _passkey = 0;

// The stream: one at a time, pumped from loop().
struct Stream {
    bool active = false;
    uint8_t op = 0;
    uint16_t seq = 0;
    uint32_t crc = Crc32::INIT;
    uint32_t total = 0;
    // GET
    int index = 0;
    uint32_t offset = 0;
    // LIST
    int ordinal = 0;      // Oldest-first position to continue from
    uint16_t fromIndex = 0;
    bool unsyncedOnly = false;
    bool abortRequested = false;
} _stream;

uint8_t _frame[FRAME_HEADER_SIZE + MAX_CHUNK];

size_t chunkSize() {
    uint16_t mtu = _peerMtu;
    if (mtu < 23) mtu = 23;
    size_t payload = (size_t)mtu - 3 - FRAME_HEADER_SIZE;
    if (payload > MAX_CHUNK) payload = MAX_CHUNK;
    return payload;
}

uint32_t epochNow() {
    time_t t = time(nullptr);
    return (t > EPOCH_PLAUSIBLE) ? (uint32_t)t : 0;
}

// ---- NimBLE callbacks: stash and return ----

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        _connected = true;
        _peerMtu = connInfo.getMTU();
        _lastBleActivity = millis();
        pushEvent(Event::Connected);
    }
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        _connected = false;
        _subscribed = false;
        _lastBleActivity = millis();
        pushEvent(Event::Disconnected);
        // Advertise again so the phone can come back. loop() notices the
        // stream and abandons it; the phone resumes with GET index offset.
        NimBLEDevice::startAdvertising();
    }
    void onMTUChange(uint16_t mtu, NimBLEConnInfo& connInfo) override {
        _peerMtu = mtu;
    }
    uint32_t onPassKeyDisplay() override {
        // A fresh 6-digit passkey per pairing, shown on the panel by main.cpp.
        // Static passkeys are a known BLE weakness and there is a screen right
        // there, so use it.
        _passkey = 100000 + (esp_random() % 900000);
        pushEvent(Event::Passkey, _passkey);
        return _passkey;
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        bool ok = connInfo.isEncrypted();
        pushEvent(Event::PairingDone, ok ? 1 : 0);
        if (!ok) {
            NimBLEDevice::getServer()->disconnect(connInfo.getConnHandle());
        }
    }
};

class ControlCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue v = c->getValue();
        if (v.size() == 0 || v.size() > sizeof(_cmd.bytes)) return;
        if (_cmdPending) return;  // One at a time; the phone waits for END anyway
        memcpy(_cmd.bytes, v.data(), v.size());
        _cmd.len = (uint8_t)v.size();
        _cmdPending = true;
        _lastBleActivity = millis();
    }
};

class DataCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        _subscribed = (subValue & 0x01) != 0;
        _lastBleActivity = millis();
    }
};

ServerCallbacks _serverCallbacks;
ControlCallbacks _controlCallbacks;
DataCallbacks _dataCallbacks;

// ---- loop() side ----

void refreshInfo() {
    Info i;
    const char* id = Identity::cameraId();
    for (int k = 0; k < 6; k++) {
        char pair[3] = {id[k * 2], id[k * 2 + 1], 0};
        i.mac[k] = (uint8_t)strtoul(pair, nullptr, 16);
    }
    i.photoCount = (uint16_t)Storage::photoCount();
    i.unsyncedCount = (uint16_t)Storage::unsyncedCount();
    int newest = Storage::newestIndex();
    i.newestIndex = (uint16_t)(newest > 0 ? newest : 0);
    i.boot = Storage::bootCount();
    i.uptimeMs = millis();
    i.epoch = epochNow();
    i.flags = 0;
    if (i.epoch) i.flags |= INFO_TIME_VALID;
    if (Storage::totalBytes() > 0) i.flags |= INFO_STORAGE_OK;
    if (_stream.active) i.flags |= INFO_BUSY;
    uint8_t packed[INFO_SIZE];
    packInfo(i, packed);
    _info->setValue(packed, sizeof(packed));
    _lastInfoRefresh = millis();
}

// Push one frame. Returns false if the stack refused it (queue full); the
// caller retries next loop().
bool sendFrame(uint8_t kind, const uint8_t* payload, size_t len) {
    packFrameHeader(kind, _stream.seq, _frame);
    if (len) memcpy(_frame + FRAME_HEADER_SIZE, payload, len);
    _data->setValue(_frame, FRAME_HEADER_SIZE + len);
    if (!_data->notify()) return false;
    _stream.seq++;
    _lastBleActivity = millis();
    return true;
}

void endStream(uint8_t status) {
    uint8_t payload[END_PAYLOAD_SIZE];
    packEnd(_stream.op, status, _stream.total, Crc32::finish(_stream.crc), payload);
    // Best effort: if even the END won't go, the phone's timeout covers it.
    sendFrame(KIND_END, payload, sizeof(payload));
    _stream.active = false;
}

void startStream(uint8_t op) {
    _stream = Stream();
    _stream.active = true;
    _stream.op = op;
}

// One-shot reply for commands that don't stream anything.
void reply(uint8_t op, uint8_t status) {
    startStream(op);
    endStream(status);
}

void handleCommand(const uint8_t* b, size_t len) {
    uint8_t op = b[0];
    switch (op) {
        case OP_SET_TIME: {
            if (len < 5) return reply(op, STATUS_ERROR);
            struct timeval tv;
            tv.tv_sec = (time_t)get32(b + 1);
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
            Serial.printf("Sync: clock set to %lu\n", (unsigned long)tv.tv_sec);
            return reply(op, STATUS_OK);
        }
        case OP_LIST: {
            if (len < 4) return reply(op, STATUS_ERROR);
            if (_stream.active) return reply(op, STATUS_BUSY);
            startStream(op);
            _stream.fromIndex = get16(b + 1);
            _stream.unsyncedOnly = (b[3] & LIST_FLAG_UNSYNCED_ONLY) != 0;
            _stream.ordinal = 0;
            return;
        }
        case OP_GET: {
            if (len < 7) return reply(op, STATUS_ERROR);
            if (_stream.active) return reply(op, STATUS_BUSY);
            int index = get16(b + 1);
            if (!Storage::exists(index)) return reply(op, STATUS_NOT_FOUND);
            startStream(op);
            _stream.index = index;
            _stream.offset = get32(b + 3);
            return;
        }
        case OP_ACK: {
            if (len < 3) return reply(op, STATUS_ERROR);
            int index = get16(b + 1);
            if (!Storage::exists(index)) return reply(op, STATUS_NOT_FOUND);
            if (!Storage::markSynced(index)) return reply(op, STATUS_ERROR);
            _sentThisConnection++;
            pushEvent(Event::Sent, (uint32_t)_sentThisConnection);
            Serial.printf("Sync: #%04d acked\n", index);
            refreshInfo();
            return reply(op, STATUS_OK);
        }
        case OP_DELETE: {
            if (len < 3) return reply(op, STATUS_ERROR);
            int index = get16(b + 1);
            if (!Storage::exists(index)) return reply(op, STATUS_NOT_FOUND);
            bool ok = Storage::deletePhoto(index);
            refreshInfo();
            return reply(op, ok ? STATUS_OK : STATUS_ERROR);
        }
        case OP_ABORT: {
            if (_stream.active) _stream.abortRequested = true;
            else reply(op, STATUS_OK);
            return;
        }
        default:
            return reply(op, STATUS_ERROR);
    }
}

// Pump the active stream: up to CHUNKS_PER_LOOP frames, stopping early when
// the stack's queue is full.
void pump() {
    if (!_stream.active) return;
    if (!_connected) {
        // Phone gone mid-stream. Nothing to tell; it resumes by offset.
        _stream.active = false;
        return;
    }
    if (_stream.abortRequested) {
        endStream(STATUS_ABORTED);
        return;
    }
    const size_t chunk = chunkSize();
    uint8_t payload[MAX_CHUNK];

    for (int n = 0; n < CHUNKS_PER_LOOP && _stream.active; n++) {
        if (_stream.op == OP_GET) {
            int got = Storage::readPhotoChunk(_stream.index, _stream.offset, payload, chunk);
            if (got < 0) { endStream(STATUS_ERROR); return; }
            if (got == 0) { endStream(STATUS_OK); return; }
            if (!sendFrame(KIND_PHOTO_DATA, payload, (size_t)got)) return;  // Retry next loop
            _stream.crc = Crc32::update(_stream.crc, payload, (size_t)got);
            _stream.total += (uint32_t)got;
            _stream.offset += (uint32_t)got;
        } else if (_stream.op == OP_LIST) {
            // Fill one frame with as many entries as fit.
            const size_t perFrame = chunk / LIST_ENTRY_SIZE;
            size_t used = 0;
            const uint16_t bootNow = Storage::bootCount();
            const uint32_t upNow = millis();
            const uint32_t epNow = epochNow();
            while (used / LIST_ENTRY_SIZE < perFrame && _stream.ordinal < Storage::photoCount()) {
                int index = Storage::photoAtOldest(_stream.ordinal++);
                if (index < (int)_stream.fromIndex) continue;
                Storage::PhotoInfo pi;
                if (!Storage::photoInfo(index, &pi)) continue;
                if (_stream.unsyncedOnly && pi.synced) continue;
                ListEntry e;
                e.index = (uint16_t)index;
                e.flags = pi.synced ? ENTRY_SYNCED : 0;
                e.size = pi.size;
                e.uptimeMs = pi.uptimeMs;
                e.epoch = estimateEpoch(pi.epoch, pi.boot, pi.uptimeMs, bootNow, upNow, epNow, &e.flags);
                packListEntry(e, payload + used);
                used += LIST_ENTRY_SIZE;
            }
            if (used == 0) { endStream(STATUS_OK); return; }
            if (!sendFrame(KIND_LIST_DATA, payload, used)) {
                // Couldn't send: rewind the cursor by what we packed. Simplest
                // correct thing is to redo this frame next loop.
                _stream.ordinal -= (int)(used / LIST_ENTRY_SIZE);
                if (_stream.ordinal < 0) _stream.ordinal = 0;
                return;
            }
            _stream.crc = Crc32::update(_stream.crc, payload, used);
            _stream.total += (uint32_t)used;
        } else {
            endStream(STATUS_ERROR);
            return;
        }
    }
}

}  // namespace

void begin() {
    if (_running) return;

    NimBLEDevice::init(Identity::deviceName());
    NimBLEDevice::setMTU(MTU_REQUEST);
    // Bonded, authenticated (passkey on the panel), secure connections. The
    // secret characteristic demands all three; the rest of the service is
    // harmless to read.
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

    _server = NimBLEDevice::createServer();
    // deleteCallbacks = false: these are statics, and the server would
    // otherwise delete them on deinit() before every sleep.
    _server->setCallbacks(&_serverCallbacks, false);
    // Don't auto-advertise on disconnect from inside the stack; the
    // disconnect callback does it explicitly so the behaviour is in one place.
    _server->advertiseOnDisconnect(false);

    NimBLEService* svc = _server->createService(SERVICE_UUID);
    _info = svc->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ);
    _secret = svc->createCharacteristic(
        SECRET_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
    _control = svc->createCharacteristic(CONTROL_UUID, NIMBLE_PROPERTY::WRITE);
    _data = svc->createCharacteristic(DATA_UUID, NIMBLE_PROPERTY::NOTIFY);

    _secret->setValue(Identity::secret(), SECRET_SIZE);
    _control->setCallbacks(&_controlCallbacks);
    _data->setCallbacks(&_dataCallbacks);
    svc->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setName(Identity::deviceName());
    adv->addServiceUUID(SERVICE_UUID);
    // 100ms: quick enough for a phone to notice within its scan window,
    // cheap enough for the few seconds the camera is awake. Units of 0.625ms.
    adv->setMinInterval(160);
    adv->setMaxInterval(160);
    adv->enableScanResponse(true);

    _stream = Stream();
    _cmdPending = false;
    _sentThisConnection = 0;
    refreshInfo();

    adv->start();
    _running = true;
    Serial.printf("Sync: advertising as %s\n", Identity::deviceName());
}

void end() {
    if (!_running) return;
    _stream.active = false;
    NimBLEDevice::stopAdvertising();
    // deinit(true) frees everything, so begin() rebuilds from scratch. Simpler
    // than keeping server objects alive across a sleep and hoping the
    // controller agrees with them afterwards.
    NimBLEDevice::deinit(true);
    // Belt and braces. NimBLE's deinit is supposed to take the controller
    // down with the host, but a controller left enabled keeps its own
    // light-sleep wake source armed, and the camera then wakes the instant
    // it sleeps (wake cause BT, or the sleep call rejected outright). Make
    // sure it is really off, and drop the wake source it may have left.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_BT);
    _server = nullptr;
    _info = _secret = _control = _data = nullptr;
    _connected = false;
    _subscribed = false;
    _running = false;
    Serial.println("Sync: stopped");
}

void loop() {
    if (!_running) return;

    if (_cmdPending) {
        PendingCommand cmd;
        memcpy(&cmd, &_cmd, sizeof(cmd));
        _cmdPending = false;
        // Handled even before the phone subscribes to Data: SET_TIME needs no
        // answer, and a reply to an unsubscribed phone is a notify() that
        // simply returns false.
        handleCommand(cmd.bytes, cmd.len);
    }

    pump();

    if (millis() - _lastInfoRefresh >= INFO_REFRESH_MS) refreshInfo();
}

bool nextEvent(Event* out) {
    if (_evTail == _evHead) return false;
    *out = _events[_evTail];
    _evTail = (_evTail + 1) % EVENT_QUEUE;
    if (out->kind == Event::Connected) _sentThisConnection = 0;
    return true;
}

bool connected() { return _connected; }
bool busy() { return _stream.active; }

bool keepAwake(uint32_t now, uint32_t idleForMs) {
    if (!_running) return false;
    if (_connected) return now - _lastBleActivity < BLE_KEEPAWAKE_MS;
    return Storage::unsyncedCount() > 0 && idleForMs < UNSYNCED_LINGER_MS;
}

}
