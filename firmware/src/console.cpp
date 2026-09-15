#include "console.h"

#include "crc32.h"
#include "identity.h"
#include "storage.h"

namespace Console {

namespace {

char _line[32];
size_t _len = 0;

// Hex, 64 bytes per line. Twice the bytes of a binary dump, but the file is
// 10KB and USB-CDC is fast; in exchange the transfer is safe against anything
// the terminal or the serial layer might do to control characters.
constexpr size_t CHUNK = 64;

void cmdLs() {
    for (int o = 0; o < Storage::photoCount(); o++) {
        int index = Storage::photoAtOldest(o);
        Storage::PhotoInfo info;
        if (!Storage::photoInfo(index, &info)) continue;
        Serial.printf("%d %lu %d\n", index, (unsigned long)info.size, info.synced ? 1 : 0);
    }
    Serial.println("ok");
}

void cmdGet(int index) {
    Storage::PhotoInfo info;
    if (index <= 0 || !Storage::photoInfo(index, &info)) {
        Serial.println("err not found");
        return;
    }
    Serial.printf("begin %d %lu\n", index, (unsigned long)info.size);
    uint8_t buf[CHUNK];
    char hex[CHUNK * 2 + 1];
    uint32_t crc = Crc32::INIT;
    uint32_t offset = 0;
    for (;;) {
        int n = Storage::readPhotoChunk(index, offset, buf, sizeof(buf));
        if (n < 0) {
            Serial.println("err read");
            return;
        }
        if (n == 0) break;
        crc = Crc32::update(crc, buf, (size_t)n);
        for (int i = 0; i < n; i++) snprintf(hex + i * 2, 3, "%02x", buf[i]);
        Serial.println(hex);
        offset += (uint32_t)n;
    }
    Serial.printf("end %08lx\n", (unsigned long)Crc32::finish(crc));
}

void cmdStat() {
    Serial.printf("photos=%d unsynced=%d used=%u total=%u boot=%u id=%s code=%s\n",
                  Storage::photoCount(), Storage::unsyncedCount(),
                  (unsigned)Storage::usedBytes(), (unsigned)Storage::totalBytes(),
                  (unsigned)Storage::bootCount(), Identity::cameraId(), Identity::shortCode());
}

void run(const char* line) {
    if (!strcmp(line, "ls")) {
        cmdLs();
    } else if (!strncmp(line, "get ", 4)) {
        cmdGet(atoi(line + 4));
    } else if (!strcmp(line, "stat")) {
        cmdStat();
    } else if (line[0]) {
        Serial.println("err unknown");
    }
}

}  // namespace

void begin() {
    _len = 0;
}

bool poll() {
    bool ran = false;
    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            _line[_len] = 0;
            run(_line);
            _len = 0;
            ran = true;
        } else if (_len < sizeof(_line) - 1) {
            _line[_len++] = (char)c;
        } else {
            // Overlong line: drop it rather than execute a truncated command.
            _len = 0;
        }
    }
    return ran;
}

}
