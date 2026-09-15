#pragma once

// PBM (P4) header reading and writing, shared by storage, the serial console,
// the host preview tool and the host tests. Deliberately free of Arduino so it
// compiles anywhere with a C++ compiler.
//
// The firmware writes one comment line between the magic and the dimensions:
//
//     P4
//     # boot=17 up=48213 t=1757789000
//     320 240
//
// boot = NVS boot counter, up = millis() at capture, t = unix time at capture
// or 0 when the camera did not know the time. Files from older firmware have no
// comment line; both shapes parse. See docs/protocol.md §2.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

namespace Pbm {

// Longest header this code ever writes: "P4\n# boot=65535 up=4294967295
// t=4294967295\n65535 65535\n" is 56 bytes. Readers only need this many bytes
// to find the raster.
constexpr size_t MAX_HEADER = 64;

struct Header {
    int width = 0;
    int height = 0;
    bool hasMeta = false;
    uint16_t boot = 0;
    uint32_t up = 0;
    uint32_t t = 0;
    size_t rasterOffset = 0;  // Byte offset of the first raster byte
};

// Parse a header from the first `len` bytes of a file. Returns false if the
// bytes are not a binary PBM or the header is not complete within `len`.
inline bool parseHeader(const uint8_t* buf, size_t len, Header* out) {
    if (!buf || !out || len < 3) return false;
    if (buf[0] != 'P' || buf[1] != '4') return false;

    Header h;
    size_t pos = 2;
    int values[2];
    int found = 0;

    while (found < 2) {
        if (pos >= len) return false;
        uint8_t c = buf[pos];
        if (c == '#') {
            // Comment runs to end of line. Pick key=value pairs out of it;
            // unknown keys are ignored so the format can grow.
            size_t end = pos;
            while (end < len && buf[end] != '\n') end++;
            if (end >= len) return false;
            size_t i = pos + 1;
            while (i < end) {
                while (i < end && buf[i] == ' ') i++;
                size_t kStart = i;
                while (i < end && buf[i] != '=' && buf[i] != ' ') i++;
                if (i < end && buf[i] == '=') {
                    size_t kLen = i - kStart;
                    i++;
                    char num[16];
                    size_t n = 0;
                    while (i < end && buf[i] != ' ' && n < sizeof(num) - 1) num[n++] = (char)buf[i++];
                    num[n] = 0;
                    unsigned long v = strtoul(num, nullptr, 10);
                    if (kLen == 4 && !memcmp(buf + kStart, "boot", 4)) { h.boot = (uint16_t)v; h.hasMeta = true; }
                    else if (kLen == 2 && !memcmp(buf + kStart, "up", 2)) { h.up = (uint32_t)v; h.hasMeta = true; }
                    else if (kLen == 1 && buf[kStart] == 't') { h.t = (uint32_t)v; h.hasMeta = true; }
                } else {
                    while (i < end && buf[i] != ' ') i++;
                }
            }
            pos = end + 1;
        } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            pos++;
        } else if (c >= '0' && c <= '9') {
            int v = 0;
            while (pos < len && buf[pos] >= '0' && buf[pos] <= '9') {
                v = v * 10 + (buf[pos] - '0');
                pos++;
            }
            values[found++] = v;
        } else {
            return false;
        }
    }
    // Exactly one whitespace byte separates the header from the raster.
    if (pos >= len) return false;
    pos++;

    h.width = values[0];
    h.height = values[1];
    h.rasterOffset = pos;
    if (h.width <= 0 || h.height <= 0) return false;
    *out = h;
    return true;
}

// Write the firmware's header shape. Returns the length written, or -1 if it
// did not fit. `t` of 0 means "time unknown" and is written as such, so a
// reader can tell an unset clock from the epoch.
inline int writeHeader(char* out, size_t outLen, int width, int height,
                       uint16_t boot, uint32_t up, uint32_t t) {
    int n = snprintf(out, outLen, "P4\n# boot=%u up=%lu t=%lu\n%d %d\n",
                     (unsigned)boot, (unsigned long)up, (unsigned long)t, width, height);
    if (n < 0 || (size_t)n >= outLen) return -1;
    return n;
}

inline size_t rasterBytes(const Header& h) {
    return (size_t)((h.width + 7) / 8) * (size_t)h.height;
}

}  // namespace Pbm
