#include "storage.h"

#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

#include "pbm_header.h"

namespace Storage {

namespace {

// LittleFS rather than SPIFFS: it survives power loss mid-write, which matters
// on a device whose only power switch is the battery running out. It mounts the
// partition the table still calls "spiffs" — that's a label, not a format.
// These mount parameters are frozen: photos already on devices depend on them.
constexpr const char* PARTITION_LABEL = "spiffs";
constexpr const char* EXTENSION = ".pbm";
constexpr char SYNCED_MARK = 's';

// LittleFS allocates in blocks and needs room for metadata, so a write can fail
// even when the arithmetic says it fits. Keep a block in reserve.
constexpr size_t FREE_SPACE_SLACK = 4096;

// Rows are inverted through a stack buffer on the way in and out; this caps
// how wide an image this code will take. 320px needs 40.
constexpr int MAX_STRIDE = 64;

// The partition holds ~120 photos at 12KB each (LittleFS rounds a 9.6KB file
// up to three 4KB blocks). 256 slots is comfortable and costs 768 bytes.
constexpr int MAX_PHOTOS = 256;

// Clock sanity: time() before anyone set it counts up from 1970. Anything
// before this (2020-09-13) is treated as "clock unset" and stamped as 0.
constexpr time_t EPOCH_PLAUSIBLE = 1600000000;

struct Entry {
    uint16_t index;
    bool synced;
};

bool _mounted = false;
int _nextIndex = 1;
Entry _entries[MAX_PHOTOS];  // Ascending by index
int _count = 0;
uint16_t _boot = 0;

void photoPath(int index, bool synced, char* out, size_t outLen) {
    snprintf(out, outLen, "/%04d%s%s", index, synced ? "s" : "", EXTENSION);
}

// "/0007.pbm" or "/0007s.pbm" -> 7. Anything else -> -1.
int parseName(const char* name, bool* synced) {
    if (name[0] == '/') name++;
    int index = 0;
    int digits = 0;
    while (*name >= '0' && *name <= '9') {
        index = index * 10 + (*name - '0');
        name++;
        digits++;
    }
    if (digits == 0 || index <= 0) return -1;
    bool s = false;
    if (*name == SYNCED_MARK) {
        s = true;
        name++;
    }
    if (strcmp(name, EXTENSION) != 0) return -1;
    if (synced) *synced = s;
    return index;
}

int findSlot(int index) {
    for (int i = 0; i < _count; i++) {
        if (_entries[i].index == index) return i;
    }
    return -1;
}

void insertEntry(int index, bool synced) {
    if (_count >= MAX_PHOTOS) return;
    int pos = _count;
    while (pos > 0 && _entries[pos - 1].index > index) {
        _entries[pos] = _entries[pos - 1];
        pos--;
    }
    _entries[pos].index = (uint16_t)index;
    _entries[pos].synced = synced;
    _count++;
}

void removeSlot(int slot) {
    for (int i = slot; i < _count - 1; i++) _entries[i] = _entries[i + 1];
    _count--;
}

File openPhoto(int index, int* slotOut = nullptr) {
    int slot = findSlot(index);
    if (slot < 0) return File();
    if (slotOut) *slotOut = slot;
    char path[24];
    photoPath(index, _entries[slot].synced, path, sizeof(path));
    return LittleFS.open(path, FILE_READ);
}

bool readHeader(File& f, Pbm::Header* h) {
    uint8_t buf[Pbm::MAX_HEADER];
    size_t n = f.read(buf, sizeof(buf));
    return Pbm::parseHeader(buf, n, h);
}

}  // namespace

bool init() {
    // Format on failure: a corrupt or never-initialised partition is the normal
    // first-boot state, and there is nothing on it worth refusing to boot over.
    if (!LittleFS.begin(true, "/littlefs", 10, PARTITION_LABEL)) {
        Serial.println("Storage: mount failed");
        return false;
    }
    _mounted = true;

    // Boot counter, so a photo's `up=` can be turned into a real time later if
    // the phone connects before the next reboot (docs/protocol.md §2). One NVS
    // write per boot.
    Preferences prefs;
    if (prefs.begin("storage", false)) {
        _boot = (uint16_t)(prefs.getUShort("boot", 0) + 1);
        prefs.putUShort("boot", _boot);
        prefs.end();
    }

    // Derive the index table from what's on disk rather than keeping it in
    // NVS. One directory scan at boot is cheaper than a flash write per photo,
    // and it can't drift out of sync with the actual files.
    _count = 0;
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        bool synced = false;
        int index = parseName(f.name(), &synced);
        if (index <= 0) continue;
        insertEntry(index, synced);
        if (index >= _nextIndex) _nextIndex = index + 1;
    }

    Serial.printf("Storage: %d photos (%d unsynced), %u/%u bytes used, next #%04d, boot %u\n",
                  _count, unsyncedCount(), (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes(), _nextIndex, (unsigned)_boot);
    return true;
}

Result savePhoto(const uint8_t* bits, int width, int height) {
    if (!_mounted || !bits) return Result::Error;
    if (_count >= MAX_PHOTOS) return Result::Full;

    const int stride = (width + 7) / 8;
    if (stride > MAX_STRIDE) return Result::Error;

    // Binary PBM (P4) is packed 1-bit rows behind a short ASCII header — the
    // same layout the display already keeps, so writing it costs nothing over a
    // raw dump but the files open in any image viewer. The comment line carries
    // when the shot was taken, as well as this device can know it.
    time_t now = time(nullptr);
    uint32_t epoch = (now > EPOCH_PLAUSIBLE) ? (uint32_t)now : 0;
    char header[Pbm::MAX_HEADER];
    int headerLen = Pbm::writeHeader(header, sizeof(header), width, height, _boot, millis(), epoch);
    if (headerLen < 0) return Result::Error;

    const size_t needed = (size_t)headerLen + (size_t)stride * height;

    // Make room by evicting the oldest photos the phone already holds. Loops
    // because one delete may not be enough — LittleFS frees whole blocks.
    // Photos nobody has pulled are never evicted; see the header comment.
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < needed + FREE_SPACE_SLACK) {
        int victim = -1;
        for (int i = 0; i < _count; i++) {
            if (_entries[i].synced) { victim = _entries[i].index; break; }
        }
        if (victim < 0) {
            Serial.println("Storage: full, nothing synced to evict");
            return Result::Full;
        }
        if (!deletePhoto(victim)) return Result::Error;
        Serial.printf("Storage: evicted synced #%04d to make room\n", victim);
    }

    char path[24];
    photoPath(_nextIndex, false, path, sizeof(path));

    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) {
        Serial.printf("Storage: could not open %s\n", path);
        return Result::Error;
    }

    bool ok = f.write((const uint8_t*)header, headerLen) == (size_t)headerLen;

    // PBM is the other way round from the panel: a set bit is black there, white
    // here. Invert a row at a time rather than the whole image, so this needs no
    // second framebuffer.
    uint8_t row[MAX_STRIDE];
    for (int y = 0; y < height && ok; y++) {
        const uint8_t* src = bits + (size_t)y * stride;
        for (int i = 0; i < stride; i++) row[i] = (uint8_t)~src[i];
        ok = f.write(row, stride) == (size_t)stride;
    }

    f.close();

    if (!ok) {
        // A partial file is worse than none — the gallery would try to decode it.
        LittleFS.remove(path);
        Serial.printf("Storage: write failed, removed %s\n", path);
        return Result::Error;
    }

    Serial.printf("Storage: wrote %s (%u bytes, t=%lu)\n", path, (unsigned)needed, (unsigned long)epoch);
    insertEntry(_nextIndex, false);
    _nextIndex++;
    return Result::Ok;
}

int photoCount() { return _count; }

int unsyncedCount() {
    int n = 0;
    for (int i = 0; i < _count; i++) if (!_entries[i].synced) n++;
    return n;
}

int newestIndex() { return _count ? _entries[_count - 1].index : -1; }

int photoAt(int ordinal) {
    if (ordinal < 0 || ordinal >= _count) return -1;
    return _entries[_count - 1 - ordinal].index;
}

int photoAtOldest(int ordinal) {
    if (ordinal < 0 || ordinal >= _count) return -1;
    return _entries[ordinal].index;
}

bool exists(int index) { return findSlot(index) >= 0; }

bool isSynced(int index) {
    int slot = findSlot(index);
    return slot >= 0 && _entries[slot].synced;
}

bool photoInfo(int index, PhotoInfo* out) {
    if (!_mounted || !out) return false;
    int slot;
    File f = openPhoto(index, &slot);
    if (!f) return false;
    Pbm::Header h;
    bool ok = readHeader(f, &h);
    size_t size = f.size();
    f.close();
    if (!ok) return false;
    out->index = (uint16_t)index;
    out->size = (uint32_t)size;
    out->synced = _entries[slot].synced;
    out->hasMeta = h.hasMeta;
    out->boot = h.boot;
    out->uptimeMs = h.up;
    out->epoch = h.t;
    return true;
}

bool loadPhoto(int index, uint8_t* bits, size_t len) {
    if (!_mounted || !bits) return false;
    File f = openPhoto(index);
    if (!f) return false;
    Pbm::Header h;
    if (!readHeader(f, &h) || Pbm::rasterBytes(h) != len) {
        f.close();
        return false;
    }
    const int stride = (h.width + 7) / 8;
    if (stride > MAX_STRIDE || !f.seek(h.rasterOffset)) {
        f.close();
        return false;
    }
    uint8_t row[MAX_STRIDE];
    bool ok = true;
    for (int y = 0; y < h.height && ok; y++) {
        ok = f.read(row, stride) == (size_t)stride;
        uint8_t* dst = bits + (size_t)y * stride;
        for (int i = 0; i < stride; i++) dst[i] = (uint8_t)~row[i];
    }
    f.close();
    return ok;
}

int readPhotoChunk(int index, uint32_t offset, uint8_t* out, size_t len) {
    if (!_mounted || !out) return -1;
    File f = openPhoto(index);
    if (!f) return -1;
    if (offset >= f.size()) {
        f.close();
        return 0;
    }
    if (!f.seek(offset)) {
        f.close();
        return -1;
    }
    int n = (int)f.read(out, len);
    f.close();
    return n;
}

bool markSynced(int index) {
    if (!_mounted) return false;
    int slot = findSlot(index);
    if (slot < 0) return false;
    if (_entries[slot].synced) return true;
    char from[24], to[24];
    photoPath(index, false, from, sizeof(from));
    photoPath(index, true, to, sizeof(to));
    // rename() is atomic in LittleFS: the photo is either marked or not, never
    // half-gone, whatever the battery does mid-way.
    if (!LittleFS.rename(from, to)) {
        Serial.printf("Storage: could not mark %s synced\n", from);
        return false;
    }
    _entries[slot].synced = true;
    return true;
}

bool deletePhoto(int index) {
    if (!_mounted) return false;
    int slot = findSlot(index);
    if (slot < 0) return false;
    char path[24];
    photoPath(index, _entries[slot].synced, path, sizeof(path));
    if (!LittleFS.remove(path)) {
        Serial.printf("Storage: could not remove %s\n", path);
        return false;
    }
    removeSlot(slot);
    return true;
}

uint16_t bootCount() { return _boot; }
size_t usedBytes() { return _mounted ? LittleFS.usedBytes() : 0; }
size_t totalBytes() { return _mounted ? LittleFS.totalBytes() : 0; }

}
