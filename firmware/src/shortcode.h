#pragma once

// The 6-character code a human can type instead of photographing the QR.
// Derived from the camera id exactly as docs/protocol.md §1 says, so the app
// and the server arrive at the same six letters independently. Header-only and
// Arduino-free so the host tests can check it against the doc's vectors.

#include <stdint.h>
#include <string.h>
#include "sha256.h"

namespace ShortCode {

// 32 symbols, no 0/O/1/I. Index 0 = 'A'.
constexpr const char* ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

// cameraId: 12 lowercase hex characters. out: 7 bytes (6 + NUL).
inline void compute(const char* cameraId, char out[7]) {
    uint8_t h[32];
    Sha256::hash((const uint8_t*)cameraId, strlen(cameraId), h);
    // First 40 bits big-endian, six 5-bit groups taken from the top.
    uint64_t v = 0;
    for (int i = 0; i < 5; i++) v = (v << 8) | h[i];
    for (int i = 0; i < 6; i++) out[i] = ALPHABET[(v >> (35 - 5 * i)) & 31];
    out[6] = 0;
}

}  // namespace ShortCode
