#include "sync.h"

#include <NimBLEDevice.h>
#include <esp_bt.h>
#include <esp_random.h>
#include <sys/time.h>
#include <time.h>

#include "crc32.h"
#include "identity.h"
#include "ota.h"
#include "storage.h"
#include "sync_protocol.h"
#include "version.h"

namespace Sync {

namespace {

using namespace SyncProto;

// A connected phone that has gone quiet for this long stops holding the
// radio on. A stuck app cannot pin the battery; end() drops the link.
constexpr uint32_t BLE_ACTIVE_MS = 60000;
// Notifications pushed per loop() call. Enough to saturate the link at a
// 15-30ms connection interval, few enough that the viewfinder keeps drawing.
constexpr int CHUNKS_PER_LOOP = 8;
// How often loop() re-packs the Info characteristic. Uptime in it is at most
// this stale, which the dating rule can live with.
constexpr uint32_t INFO_REFRESH_MS = 500;
// Same plausibility bar as storage: below this the clock was never set.
constexpr time_t EPOCH_PLAUSIBLE = 1600000000;

constexpr int EVENT_QUEUE = 8;

// A connected phone that stops sending image chunks for this long has given
// up on the update; drop the session rather than sit on an open flash handle.
constexpr uint32_t UPDATE_IDLE_MS = 20000;
// A phone that vanishes mid-image gets this long to come back and resume from
// where it stopped. Re-sending a megabyte over BLE is minutes; holding a
// half-written *inactive* slot costs nothing but the wait.
constexpr uint32_t UPDATE_RESUME_MS = 90000;
// Floor between two "you skipped a byte" answers. Everything the phone had in
// flight when a chunk was dropped arrives as a gap too, and one rewind is
// enough to fix all of them.
constexpr uint32_t UPDATE_NAK_MS = 200;

// ---- state shared between the NimBLE task and loop() ----
// Written in callbacks, read in loop(). Single-writer per field, and the
// pending command is a one-slot mailbox guarded by its flag, so plain
// volatiles are enough.

struct PendingCommand {
    uint8_t bytes[MAX_CONTROL];
    uint8_t len;
};
volatile bool _cmdPending = false;
PendingCommand _cmd;
// Whether the connection that wrote it is bonded and authenticated. Only the
// update opcodes ask: pushing firmware is the one thing on this service that
// must not be possible for a stranger in the room.
volatile bool _cmdAuthenticated = false;

// Firmware chunks, staged between the BLE task and the main loop. The task
// copies a write in and publishes it; the loop takes them out and writes them
// to flash. Single producer, single consumer, so the index each side owns is
// the only thing that moves — but head must not become visible before the
// bytes it points at, hence the release/acquire pair rather than a bare
// volatile. (The one-slot command mailbox above gets away with less because a
// command is re-read under its flag and is seven bytes, not five hundred.)
struct UpdateChunk {
    uint32_t offset;
    uint16_t len;
    uint8_t data[MAX_UPDATE_CHUNK];
};
UpdateChunk _ring[UPDATE_SLOTS];
uint16_t _ringHead = 0;   // BLE task writes, loop reads
uint16_t _ringTail = 0;   // loop writes, BLE task reads
volatile uint32_t _ringDropped = 0;

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
NimBLECharacteristic* _updateChar = nullptr;

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

// The firmware update session (docs/protocol.md §3.7). Ota owns the flash
// side and the byte count; this is what the radio needs on top of it.
struct UpdateSession {
    bool active = false;
    uint16_t seq = 0;          // UPDATE_STATUS frames sent since UPDATE_BEGIN
    uint32_t ackedAt = 0;      // Bytes accepted when the last status went out
    uint32_t lastChunkMs = 0;  // For the idle and resume timeouts
    uint32_t lastNakMs = 0;
    int lastPercent = -1;      // Only raise an event when the screen would change
} _update;

uint8_t _frame[FRAME_HEADER_SIZE + MAX_CHUNK];

size_t chunkSize() {
    uint16_t mtu = _peerMtu;
    if (mtu < 23) mtu = 23;
    size_t payload = (size_t)mtu - 3 - FRAME_HEADER_SIZE;
    if (payload > MAX_CHUNK) payload = MAX_CHUNK;
    return payload;
}

// Data bytes per Update write, and how many bytes the phone may have in
// flight. The window is the whole staging ring: what the phone is allowed to
// send while it waits for an answer is exactly what the camera can hold.
size_t updateChunkSize() {
    uint16_t mtu = _peerMtu;
    if (mtu < 23) mtu = 23;
    size_t payload = (size_t)mtu - 3 - UPDATE_HEADER_SIZE;
    if (payload > MAX_UPDATE_CHUNK) payload = MAX_UPDATE_CHUNK;
    return payload;
}

size_t updateWindow() { return updateChunkSize() * UPDATE_SLOTS; }

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
        _cmdAuthenticated = connInfo.isEncrypted() && connInfo.isAuthenticated();
        _cmdPending = true;
        _lastBleActivity = millis();
    }
};

// Firmware chunks. Copy and publish, nothing else — a flash write takes tens
// of milliseconds and belongs on the main loop, not on the radio's task.
class UpdateCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
        NimBLEAttValue v = c->getValue();
        uint32_t offset = 0;
        const int len = parseUpdateWrite(v.data(), v.size(), &offset);
        if (len <= 0) return;
        const uint16_t head = _ringHead;
        const uint16_t next = (uint16_t)((head + 1) % UPDATE_SLOTS);
        if (next == __atomic_load_n(&_ringTail, __ATOMIC_ACQUIRE)) {
            // Full: the loop is behind. Dropping is safe — every chunk carries
            // its own offset, so the loop notices the gap and asks the phone
            // to rewind. Writes without response have nowhere to push back.
            _ringDropped++;
            return;
        }
        _ring[head].offset = offset;
        _ring[head].len = (uint16_t)len;
        memcpy(_ring[head].data, v.data() + UPDATE_HEADER_SIZE, (size_t)len);
        __atomic_store_n(&_ringHead, next, __ATOMIC_RELEASE);
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
UpdateCallbacks _updateCallbacks;

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
    if (_stream.active || _update.active) i.flags |= INFO_BUSY;
    if (Ota::trial()) i.flags |= INFO_TRIAL;
    i.fwMajor = LC_VERSION_MAJOR;
    i.fwMinor = LC_VERSION_MINOR;
    i.fwPatch = LC_VERSION_PATCH;
    i.updateState = (uint8_t)Ota::state();
    i.updateSpace = Ota::freeSpace();
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

// ---- firmware update ----------------------------------------------------

// One UPDATE_STATUS frame. Doubles as the answer to a command and as the
// unsolicited progress report that lets the phone send the next window.
void sendUpdateStatus(uint8_t op, uint8_t status) {
    if (!_data) return;
    UpdateStatus u;
    u.op = op;
    u.status = status;
    u.state = (uint8_t)Ota::state();
    u.nextOffset = Ota::received();
    u.total = Ota::total();
    u.chunk = (uint16_t)updateChunkSize();
    u.window = (uint16_t)updateWindow();
    packFrameHeader(KIND_UPDATE_STATUS, _update.seq, _frame);
    packUpdateStatus(u, _frame + FRAME_HEADER_SIZE);
    _data->setValue(_frame, FRAME_HEADER_SIZE + UPDATE_STATUS_SIZE);
    // A frame the stack refused never went out, so it must not consume a
    // sequence number either.
    if (_data->notify()) _update.seq++;
    _update.ackedAt = Ota::received();
    _lastBleActivity = millis();
}

void endUpdate(uint8_t status, Event::Kind kind) {
    if (status != STATUS_OK) Ota::abort();
    _update.active = false;
    pushEvent(kind, status);
}

// Write what the BLE task staged. Returns true if a chunk arrived out of
// order, i.e. one was dropped and the phone has to come back for it.
bool drainUpdateRing() {
    bool gap = false;
    for (;;) {
        const uint16_t tail = _ringTail;
        if (tail == __atomic_load_n(&_ringHead, __ATOMIC_ACQUIRE)) break;
        const UpdateChunk& c = _ring[tail];
        const uint32_t want = Ota::received();
        if (c.offset == want) {
            if (!Ota::write(c.data, c.len)) {
                __atomic_store_n(&_ringTail, (uint16_t)((tail + 1) % UPDATE_SLOTS), __ATOMIC_RELEASE);
                sendUpdateStatus(OP_PROGRESS, Ota::lastStatus());
                endUpdate(Ota::lastStatus(), Event::UpdateFailed);
                return false;
            }
            _update.lastChunkMs = millis();
        } else if (c.offset > want) {
            // The one before it was dropped. Everything already in flight is
            // ahead too, so note it once and answer after the drain.
            gap = true;
        }
        // c.offset < want: a duplicate from before a rewind. Already on flash.
        __atomic_store_n(&_ringTail, (uint16_t)((tail + 1) % UPDATE_SLOTS), __ATOMIC_RELEASE);
    }
    return gap;
}

void pumpUpdate() {
    if (!_update.active) return;
    const uint32_t now = millis();

    if (!_connected) {
        // Gone mid-image. Keep the half-written slot for a while: the phone
        // reconnects, repeats UPDATE_BEGIN with the same digest and carries on
        // from where it stopped.
        if (now - _update.lastChunkMs >= UPDATE_RESUME_MS) {
            Serial.println("Sync: update abandoned, phone did not come back");
            endUpdate(STATUS_ABORTED, Event::UpdateFailed);
        }
        return;
    }

    const bool gap = drainUpdateRing();
    if (!_update.active) return;  // The drain ended it

    const uint32_t got = Ota::received();
    if (gap && now - _update.lastNakMs >= UPDATE_NAK_MS) {
        _update.lastNakMs = now;
        sendUpdateStatus(OP_PROGRESS, STATUS_OFFSET);
    } else if (got > _update.ackedAt && (got >= Ota::total() || got - _update.ackedAt >= updateWindow() / 2)) {
        // Halfway through the window: let the phone push the next half before
        // it runs out of credit, so the link never idles. And always on the
        // last byte, however little of a window it took — the phone is waiting
        // to hear that the image is complete before it sends UPDATE_END, and a
        // tail shorter than half a window would otherwise go unanswered until
        // its timeout. `got > ackedAt` keeps that from repeating every loop.
        sendUpdateStatus(OP_PROGRESS, STATUS_OK);
    }

    const int pct = Ota::percent();
    if (pct != _update.lastPercent) {
        _update.lastPercent = pct;
        pushEvent(Event::UpdateProgress, (uint32_t)pct);
    }

    if (now - _update.lastChunkMs >= UPDATE_IDLE_MS) {
        Serial.printf("Sync: update stalled at %u of %u bytes\n", (unsigned)Ota::received(),
                      (unsigned)Ota::total());
        sendUpdateStatus(OP_PROGRESS, STATUS_ABORTED);
        endUpdate(STATUS_ABORTED, Event::UpdateFailed);
    }
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
            // Optional UTC offset (minutes east) behind the epoch: local time
            // is what the mood's night rule needs (docs/mood.md). A 4-byte
            // write from an older app is still fine, it just brings no tz.
            uint32_t ev = 0;
            if (len >= 7) {
                int16_t tz = (int16_t)get16(b + 5);
                if (tz >= -720 && tz <= 840) ev = CLOCK_TZ_KNOWN | (uint16_t)tz;
            }
            pushEvent(Event::ClockSet, ev);
            Serial.printf("Sync: clock set to %lu%s\n", (unsigned long)tv.tv_sec, len >= 7 ? " with tz" : "");
            return reply(op, STATUS_OK);
        }
        case OP_LIST: {
            if (len < 4) return reply(op, STATUS_ERROR);
            if (_stream.active || _update.active) return reply(op, STATUS_BUSY);
            startStream(op);
            _stream.fromIndex = get16(b + 1);
            _stream.unsyncedOnly = (b[3] & LIST_FLAG_UNSYNCED_ONLY) != 0;
            _stream.ordinal = 0;
            return;
        }
        case OP_GET: {
            if (len < 7) return reply(op, STATUS_ERROR);
            if (_stream.active || _update.active) return reply(op, STATUS_BUSY);
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
        case OP_UPDATE_BEGIN: {
            // Every BEGIN starts the status numbering over, answered or not.
            _update.seq = 0;
            uint32_t size = 0;
            uint8_t digest[SHA256_SIZE];
            if (!parseUpdateBegin(b, len, &size, digest)) return sendUpdateStatus(op, STATUS_ERROR);
            // Firmware only travels over the bonded link the Secret read set
            // up. The characteristic demands it too; this is the check that
            // stops a stranger opening a session and sitting on it.
            if (!_cmdAuthenticated) {
                Serial.println("Sync: refusing an update over an unauthenticated link");
                return sendUpdateStatus(op, STATUS_ERROR);
            }
            if (_stream.active) return sendUpdateStatus(op, STATUS_BUSY);
            const bool resuming = Ota::resumes(digest);
            if (!Ota::start(size, digest)) return sendUpdateStatus(op, Ota::lastStatus());
            // Anything staged for the old session is from another image, or
            // from before the phone rewound. Start the ring empty.
            __atomic_store_n(&_ringTail, __atomic_load_n(&_ringHead, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
            _update.lastChunkMs = millis();
            _update.lastNakMs = 0;
            if (!_update.active) {
                _update.active = true;
                _update.lastPercent = -1;
                pushEvent(Event::UpdateBegan, size);
            }
            Serial.printf("Sync: update %s at %u of %u bytes\n", resuming ? "resumes" : "begins",
                          (unsigned)Ota::received(), (unsigned)size);
            return sendUpdateStatus(op, STATUS_OK);
        }
        case OP_UPDATE_END: {
            if (!_update.active) return sendUpdateStatus(op, STATUS_ERROR);
            // Chunks written just before this command are already staged;
            // take them before deciding the image is short.
            drainUpdateRing();
            if (!_update.active) return;
            if (Ota::received() != Ota::total()) {
                // The phone got ahead of itself, or a chunk went missing right
                // at the end. Recoverable: say where the camera actually is
                // instead of condemning an image that is only incomplete.
                Serial.printf("Sync: END at %u of %u bytes\n", (unsigned)Ota::received(),
                              (unsigned)Ota::total());
                return sendUpdateStatus(op, STATUS_OFFSET);
            }
            // The screen should say 100% while the verify runs — it reads the
            // whole slot back and takes a moment.
            pushEvent(Event::UpdateProgress, 100);
            if (Ota::finish()) {
                sendUpdateStatus(op, STATUS_OK);
                _update.active = false;
                pushEvent(Event::UpdateReady, 0);
            } else {
                const uint8_t status = Ota::lastStatus();
                sendUpdateStatus(op, status);
                endUpdate(status, Event::UpdateFailed);
            }
            return;
        }
        case OP_UPDATE_ABORT: {
            if (!_update.active) return sendUpdateStatus(op, STATUS_OK);
            sendUpdateStatus(op, STATUS_ABORTED);
            endUpdate(STATUS_ABORTED, Event::UpdateFailed);
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
    // Write without response: one ATT acknowledgement per 500 bytes would
    // halve the throughput of a 1.5MB image, and the offset in every chunk
    // already makes a dropped write recoverable. Encrypted + authenticated
    // like the secret, because replacing the firmware is the most powerful
    // thing this service can be asked to do.
    _updateChar = svc->createCharacteristic(
        UPDATE_UUID, NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);

    _secret->setValue(Identity::secret(), SECRET_SIZE);
    _control->setCallbacks(&_controlCallbacks);
    _data->setCallbacks(&_dataCallbacks);
    _updateChar->setCallbacks(&_updateCallbacks);
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
    // An update session never survives the radio going down (main.cpp does not
    // sleep while one runs), so both ends start clean.
    _update = UpdateSession();
    _ringHead = _ringTail = 0;
    _ringDropped = 0;
    refreshInfo();

    adv->start();
    _running = true;
    Serial.printf("Sync: advertising as %s\n", Identity::deviceName());
}

void end() {
    if (!_running) return;
    _running = false;
    _stream.active = false;
    // The radio is going away, so nothing can finish an image in flight. Free
    // the flash handle; Ota::abort() leaves an armed update alone, which is
    // what makes end()-then-reboot after UPDATE_END safe.
    if (_update.active) {
        Ota::abort();
        _update.active = false;
    }

    // Wind the stack down in order, and let the host task catch up between
    // steps. Going straight from stopAdvertising() to deinit(true) crashed
    // intermittently (InstrFetchProhibited, PC 0): the GAP "advertising
    // complete" event was still in flight on the host task and landed on the
    // advertising object deinit had just freed. delay() yields to that task.
    if (_server) {
        for (uint16_t handle : _server->getPeerDevices()) _server->disconnect(handle);
        // The phone is told nothing more; it reconnects when we advertise again.
        for (int i = 0; i < 30 && _connected; i++) delay(10);
    }
    NimBLEDevice::stopAdvertising();
    delay(100);

    // deinit(true) frees everything, so begin() rebuilds from scratch. Simpler
    // than keeping server objects alive across a sleep and hoping the
    // controller agrees with them afterwards.
    NimBLEDevice::deinit(true);
    // Belt and braces. NimBLE's deinit takes the controller down with the
    // host on every core version seen so far, but a controller left enabled
    // keeps its own light-sleep wake source armed, and the camera would then
    // wake the instant it sleeps. Make sure it is really off.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) esp_bt_controller_disable();
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) esp_bt_controller_deinit();
    _server = nullptr;
    _info = _secret = _control = _data = _updateChar = nullptr;
    _connected = false;
    _subscribed = false;
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
    pumpUpdate();

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
bool busy() { return _stream.active || _update.active; }
bool updating() { return _update.active; }

bool activeRecently(uint32_t now) {
    return _running && _connected && now - _lastBleActivity < BLE_ACTIVE_MS;
}

}
