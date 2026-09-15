#pragma once

#include <Arduino.h>

// Photo storage on the internal flash: the 1.5MB `spiffs` partition, mounted
// as LittleFS, holding 320x240 1-bit PBM files named /0007.pbm. Once the
// phone has pulled a photo the file is renamed /0007s.pbm — the `s` is the
// only sync bookkeeping there is, it lives on disk with the photo, and it
// survives reboots, reflashes and whatever the app does. See docs/protocol.md
// §2 for the file format and firmware/AGENTS.md for why none of this may
// change shape.
//
// Eviction: when the partition is full, saving a photo deletes the oldest
// *synced* photos until the new one fits. If nothing synced is left it refuses
// (Result::Full) rather than destroying a photo nobody has a copy of. That is
// the opposite of the original ring, which kept the shutter working at any
// cost; now that photos can leave the device, losing one silently is worse than
// being told to sync.
namespace Storage {

enum class Result {
    Ok,
    Full,      // No room and nothing synced to evict — sync the phone, then shoot
    Error,     // Filesystem not mounted, or the write failed
};

struct PhotoInfo {
    uint16_t index;    // File number, /0007.pbm -> 7
    uint32_t size;     // Bytes on disk, header included
    bool synced;       // File carries the `s` suffix
    bool hasMeta;      // Header had the boot/up/t comment (newer firmware)
    uint16_t boot;     // Boot counter when captured
    uint32_t uptimeMs; // millis() when captured
    uint32_t epoch;    // Unix time when captured, 0 if the clock was unset
};

// Mounts the filesystem, builds the index table and bumps the boot counter.
// Safe to call once from setup(); everything else no-ops if this failed.
bool init();

// Write a 1-bit bitmap as the next numbered photo. `bits` is packed MSB-first
// with a set bit meaning white — i.e. exactly Display::photoBits(). Stamps the
// header with the boot counter, millis() and the clock if it has been set.
Result savePhoto(const uint8_t* bits, int width, int height);

int photoCount();
int unsyncedCount();
int newestIndex();                 // -1 if none
int photoAt(int ordinal);          // Newest-first ordinal -> file index, -1 out of range
int photoAtOldest(int ordinal);    // Oldest-first ordinal -> file index, -1 out of range
bool exists(int index);
bool isSynced(int index);

// Header-only read.
bool photoInfo(int index, PhotoInfo* out);

// Full read into a panel-polarity bitmap (set bit = white), `len` must equal
// the raster size (9600 for 320x240). Rejects other dimensions.
bool loadPhoto(int index, uint8_t* bits, size_t len);

// Raw file bytes (header + raster, a valid PBM) from `offset`. Returns bytes
// read, 0 at end of file, -1 on error. Stateless: open, seek, read, close.
int readPhotoChunk(int index, uint32_t offset, uint8_t* out, size_t len);

bool markSynced(int index);   // Rename to the `s` form
bool deletePhoto(int index);

uint16_t bootCount();
size_t usedBytes();
size_t totalBytes();

}
