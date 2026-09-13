#pragma once
#include <stddef.h>
#include <stdint.h>
class Preferences {
public:
    bool begin(const char*, bool) { return false; }
    void end() {}
    uint16_t getUShort(const char*, uint16_t d) { return d; }
    size_t putUShort(const char*, uint16_t) { return 0; }
    size_t getBytesLength(const char*) { return 0; }
    size_t getBytes(const char*, void*, size_t) { return 0; }
    size_t putBytes(const char*, const void*, size_t) { return 0; }
};
