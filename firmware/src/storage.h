#pragma once

#include <Arduino.h>

// Photo storage on the internal flash. There is no SD card on this carrier and
// no modem to upload to, so saved photos live in the 1.5MB `spiffs` partition
// until something else can carry them off the device.
//
// It's a ring: once the partition is full, saving a photo deletes the oldest
// ones to make room. That keeps the shutter always working at the cost of
// silently destroying photos that were never retrieved — the right call only
// while there's no way to get files off the board anyway.
namespace Storage {

enum class Result {
    Ok,
    // Out of space with nothing left to evict — only reachable if a single
    // photo can't fit in an empty partition, so effectively a config error.
    Full,
    Error,     // Filesystem not mounted, or the write failed
};

// Mounts the filesystem and works out the next photo number. Safe to call once
// from setup(); everything else no-ops with Error if this wasn't called or
// failed.
bool init();

// Write a 1-bit bitmap as the next numbered photo. `bits` is packed MSB-first
// with a set bit meaning white — i.e. exactly Display::photoBits().
Result savePhoto(const uint8_t* bits, int width, int height);

int photoCount();

}
