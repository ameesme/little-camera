#pragma once
#include <stddef.h>
#include <stdint.h>
#define FILE_READ "r"
#define FILE_WRITE "w"
class File {
public:
    operator bool() const { return false; }
    size_t read(uint8_t*, size_t) { return 0; }
    size_t write(const uint8_t*, size_t) { return 0; }
    bool seek(uint32_t) { return false; }
    size_t size() { return 0; }
    void close() {}
    const char* name() { return ""; }
    File openNextFile() { return File(); }
};
