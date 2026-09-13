#pragma once

// IEEE CRC-32 (the zlib/PNG/Ethernet one), table-free so it costs no flash.
// Used to checksum BLE streams and serial exports; both ends of those links
// compute it, so it lives in a header the host tests can include too.
// Check value: crc32("123456789") == 0xCBF43926.

#include <stdint.h>
#include <stddef.h>

namespace Crc32 {

constexpr uint32_t INIT = 0xFFFFFFFFu;

// Feed bytes into a running CRC. Start with INIT, finish with finish().
inline uint32_t update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

inline uint32_t finish(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

inline uint32_t of(const uint8_t* data, size_t len) {
    return finish(update(INIT, data, len));
}

}  // namespace Crc32
