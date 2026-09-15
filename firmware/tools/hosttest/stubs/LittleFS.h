#pragma once
#include "FS.h"
class LittleFSClass {
public:
    bool begin(bool, const char*, int, const char*) { return false; }
    File open(const char*, const char* = FILE_READ) { return File(); }
    bool remove(const char*) { return false; }
    bool rename(const char*, const char*) { return false; }
    size_t usedBytes() { return 0; }
    size_t totalBytes() { return 0; }
};
extern LittleFSClass LittleFS;
