#include "storage.h"

#include <FS.h>
#include <LittleFS.h>

namespace Storage {

namespace {

// LittleFS rather than SPIFFS: it survives power loss mid-write, which matters
// on a device whose only power switch is the battery running out. It mounts the
// partition the table still calls "spiffs" — that's a label, not a format.
constexpr const char* PARTITION_LABEL = "spiffs";
constexpr const char* EXTENSION = ".pbm";

// LittleFS allocates in blocks and needs room for metadata, so a write can fail
// even when the arithmetic says it fits. Keep a block in reserve.
constexpr size_t FREE_SPACE_SLACK = 4096;

// Rows are inverted into a stack buffer on the way out; this caps how wide an
// image savePhoto() will take. 320px needs 40.
constexpr int MAX_STRIDE = 64;

bool _mounted = false;
int _nextIndex = 1;
int _count = 0;

void photoPath(int index, char* out, size_t outLen) {
    snprintf(out, outLen, "/%04d%s", index, EXTENSION);
}

// Lowest photo number on disk, or -1 if there are none. Numbers only ever go
// up, so the lowest is the oldest. Scanned rather than cached because it only
// matters when the partition is full, which is rare enough not to be worth
// keeping bookkeeping correct across every delete path.
int oldestIndex() {
    int oldest = -1;
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        const char* name = f.name();
        if (name[0] == '/') name++;
        int index = atoi(name);
        if (index <= 0) continue;
        if (oldest < 0 || index < oldest) oldest = index;
    }
    return oldest;
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

    // Derive the next number from what's on disk rather than keeping a counter
    // in NVS. One directory scan at boot is cheaper than a flash write per
    // photo, and it can't drift out of sync with the actual files.
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        const char* name = f.name();
        if (name[0] == '/') name++;
        int index = atoi(name);
        if (index <= 0) continue;
        _count++;
        if (index >= _nextIndex) _nextIndex = index + 1;
    }

    Serial.printf("Storage: %d photos, %u/%u bytes used, next #%04d\n",
                  _count, (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes(), _nextIndex);
    return true;
}

Result savePhoto(const uint8_t* bits, int width, int height) {
    if (!_mounted || !bits) return Result::Error;

    const int stride = (width + 7) / 8;
    if (stride > MAX_STRIDE) return Result::Error;

    // Binary PBM (P4) is packed 1-bit rows behind a short ASCII header — the
    // same layout the display already keeps, so writing it costs nothing over a
    // raw dump but the files open in any image viewer.
    char header[32];
    int headerLen = snprintf(header, sizeof(header), "P4\n%d %d\n", width, height);

    const size_t needed = (size_t)headerLen + (size_t)stride * height;

    // Ring buffer: rather than refusing the shot, drop the oldest photos until
    // there's room. Loops because one delete may not be enough — files vary in
    // size and LittleFS frees whole blocks. The tradeoff is that a photo you
    // never pulled off the device can disappear without you being told.
    while (LittleFS.totalBytes() - LittleFS.usedBytes() < needed + FREE_SPACE_SLACK) {
        int oldest = oldestIndex();
        if (oldest < 0) {
            // Nothing left to reclaim and it still doesn't fit.
            Serial.println("Storage: full with no photos to evict");
            return Result::Full;
        }
        char victim[24];
        photoPath(oldest, victim, sizeof(victim));
        if (!LittleFS.remove(victim)) {
            Serial.printf("Storage: could not evict %s\n", victim);
            return Result::Error;
        }
        Serial.printf("Storage: evicted %s to make room\n", victim);
        _count--;
    }

    char path[24];
    photoPath(_nextIndex, path, sizeof(path));

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
        // A partial file is worse than none — a future gallery would try to
        // decode it.
        LittleFS.remove(path);
        Serial.printf("Storage: write failed, removed %s\n", path);
        return Result::Error;
    }

    Serial.printf("Storage: wrote %s (%u bytes)\n", path, (unsigned)needed);
    _nextIndex++;
    _count++;
    return Result::Ok;
}

int photoCount() { return _count; }

}
