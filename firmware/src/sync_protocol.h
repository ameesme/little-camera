#pragma once

// Byte layout of the BLE sync protocol (docs/protocol.md §3), kept apart from
// the NimBLE plumbing so the host tests can check that what the camera packs is
// what the phone expects. Everything little-endian. Arduino-free.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace SyncProto {

// 1C0000xx-4C43-4D52-8000-6C6974746C65
constexpr const char* SERVICE_UUID = "1C000001-4C43-4D52-8000-6C6974746C65";
constexpr const char* INFO_UUID    = "1C000002-4C43-4D52-8000-6C6974746C65";
constexpr const char* SECRET_UUID  = "1C000003-4C43-4D52-8000-6C6974746C65";
constexpr const char* CONTROL_UUID = "1C000004-4C43-4D52-8000-6C6974746C65";
constexpr const char* DATA_UUID    = "1C000005-4C43-4D52-8000-6C6974746C65";
constexpr const char* UPDATE_UUID  = "1C000006-4C43-4D52-8000-6C6974746C65";

// 2 since Info carries the firmware version and the update state.
constexpr uint8_t VERSION = 2;

// Control opcodes
constexpr uint8_t OP_PROGRESS     = 0x00;  // Not a command: marks an unsolicited status frame
constexpr uint8_t OP_SET_TIME     = 0x01;  // u32 epoch [, i16 utc_offset_min]
constexpr uint8_t OP_LIST         = 0x02;  // u16 from_index, u8 flags (bit0 unsynced only)
constexpr uint8_t OP_GET          = 0x03;  // u16 index, u32 offset
constexpr uint8_t OP_ACK          = 0x04;  // u16 index
constexpr uint8_t OP_DELETE       = 0x05;  // u16 index
constexpr uint8_t OP_ABORT        = 0x06;
constexpr uint8_t OP_UPDATE_BEGIN = 0x10;  // u32 size, u8[32] sha256
constexpr uint8_t OP_UPDATE_END   = 0x11;
constexpr uint8_t OP_UPDATE_ABORT = 0x12;

constexpr uint8_t LIST_FLAG_UNSYNCED_ONLY = 0x01;

// Data frame kinds
constexpr uint8_t KIND_LIST_DATA     = 0x01;
constexpr uint8_t KIND_PHOTO_DATA    = 0x02;
constexpr uint8_t KIND_UPDATE_STATUS = 0x03;
constexpr uint8_t KIND_END           = 0x7F;

// END statuses
constexpr uint8_t STATUS_OK        = 0;
constexpr uint8_t STATUS_NOT_FOUND = 1;
constexpr uint8_t STATUS_ABORTED   = 2;
constexpr uint8_t STATUS_ERROR     = 3;
constexpr uint8_t STATUS_BUSY      = 4;
constexpr uint8_t STATUS_BAD_IMAGE = 5;  // Digest mismatch, or the bootloader refused the image
constexpr uint8_t STATUS_OFFSET    = 6;  // Out of order; next_offset says where to resume
constexpr uint8_t STATUS_TOO_LARGE = 7;  // Does not fit the inactive slot

// Update session states, shared by UPDATE_STATUS and Info
constexpr uint8_t UPDATE_IDLE      = 0;
constexpr uint8_t UPDATE_RECEIVING = 1;
constexpr uint8_t UPDATE_VERIFYING = 2;
constexpr uint8_t UPDATE_READY     = 3;  // Armed; the camera is about to reboot
constexpr uint8_t UPDATE_FAILED    = 4;

// Info flags
constexpr uint8_t INFO_TIME_VALID = 0x01;
constexpr uint8_t INFO_STORAGE_OK = 0x02;
constexpr uint8_t INFO_BUSY       = 0x04;
constexpr uint8_t INFO_TRIAL      = 0x08;  // Running an update that has not confirmed itself yet

// List entry flags
constexpr uint8_t ENTRY_SYNCED            = 0x01;
constexpr uint8_t ENTRY_EPOCH_IS_ESTIMATE = 0x02;
constexpr uint8_t ENTRY_EPOCH_FROM_CLOCK  = 0x04;

constexpr size_t INFO_SIZE = 32;
constexpr size_t LIST_ENTRY_SIZE = 16;
constexpr size_t FRAME_HEADER_SIZE = 3;   // u8 kind, u16 seq
constexpr size_t END_PAYLOAD_SIZE = 10;   // u8 op, u8 status, u32 total_len, u32 crc32
constexpr size_t UPDATE_STATUS_SIZE = 15; // u8 op, u8 status, u8 state, u32 next, u32 total, u16 chunk, u16 window
constexpr size_t SECRET_SIZE = 16;
constexpr size_t SHA256_SIZE = 32;
// The longest Control write: UPDATE_BEGIN's size plus the image digest.
constexpr size_t MAX_CONTROL = 1 + 4 + SHA256_SIZE;

// Largest ATT MTU we ask for. Payload per notification = MTU - 3 (ATT) - 3 (frame).
constexpr uint16_t MTU_REQUEST = 512;
constexpr size_t MAX_CHUNK = MTU_REQUEST - 3 - FRAME_HEADER_SIZE;

// Update writes are [u32 offset][data] on their own characteristic: 4 bytes of
// header instead of the frame's 3, and no sequence number, because the offset
// is the sequence number and it is what makes a dropped write recoverable
// without either side remembering anything.
constexpr size_t UPDATE_HEADER_SIZE = 4;
constexpr size_t MAX_UPDATE_CHUNK = MTU_REQUEST - 3 - UPDATE_HEADER_SIZE;
// Chunks staged in RAM between the BLE task and the main loop. The window the
// camera grants is exactly this many, so a main loop that misses a few turns
// still cannot be outrun: everything the phone may have in flight fits. Six
// slots is ~3KB of static RAM — nothing beside the 77KB camera frame — and
// enough to keep the radio busy at a 30ms connection interval.
constexpr int UPDATE_SLOTS = 6;

inline void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
inline void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

struct Info {
    uint8_t mac[6];
    uint16_t photoCount;
    uint16_t unsyncedCount;
    uint16_t newestIndex;
    uint16_t boot;
    uint32_t uptimeMs;
    uint32_t epoch;
    uint8_t flags;
    // Version 2 tail. Byte 24 used to be a reserved zero, which is why the
    // version number moved with it: a reader that knows only v1 would have
    // read fwMajor as that zero.
    uint8_t fwMajor;
    uint8_t fwMinor;
    uint8_t fwPatch;
    uint8_t updateState;   // UPDATE_IDLE..UPDATE_FAILED
    uint32_t updateSpace;  // Bytes the inactive firmware slot takes, 0 if there is none
};

inline void packInfo(const Info& i, uint8_t out[INFO_SIZE]) {
    out[0] = VERSION;
    memcpy(out + 1, i.mac, 6);
    put16(out + 7, i.photoCount);
    put16(out + 9, i.unsyncedCount);
    put16(out + 11, i.newestIndex);
    put16(out + 13, i.boot);
    put32(out + 15, i.uptimeMs);
    put32(out + 19, i.epoch);
    out[23] = i.flags;
    out[24] = i.fwMajor;
    out[25] = i.fwMinor;
    out[26] = i.fwPatch;
    out[27] = i.updateState;
    put32(out + 28, i.updateSpace);
}

// What the camera answers an UPDATE_BEGIN/END/ABORT with, and sends by itself
// every half window while an image is coming in (docs/protocol.md §3.7).
struct UpdateStatus {
    uint8_t op;           // The op being answered, or OP_PROGRESS
    uint8_t status;
    uint8_t state;
    uint32_t nextOffset;  // What the camera wants next = what it has accepted
    uint32_t total;
    uint16_t chunk;       // Most data bytes per Update write
    uint16_t window;      // Bytes the phone may have in flight beyond nextOffset
};

inline void packUpdateStatus(const UpdateStatus& u, uint8_t out[UPDATE_STATUS_SIZE]) {
    out[0] = u.op;
    out[1] = u.status;
    out[2] = u.state;
    put32(out + 3, u.nextOffset);
    put32(out + 7, u.total);
    put16(out + 11, u.chunk);
    put16(out + 13, u.window);
}

// UPDATE_BEGIN's arguments, straight out of a Control write (the leading
// opcode included). False if the write is the wrong length — the digest is
// what makes the whole update safe, so a short one is never guessed at.
inline bool parseUpdateBegin(const uint8_t* b, size_t len, uint32_t* sizeOut, uint8_t digestOut[SHA256_SIZE]) {
    if (len < 1 + 4 + SHA256_SIZE) return false;
    *sizeOut = get32(b + 1);
    memcpy(digestOut, b + 5, SHA256_SIZE);
    return true;
}

// An Update write: [u32 offset][data]. Returns the payload length, or -1 if
// there is no room for both the header and at least one byte.
inline int parseUpdateWrite(const uint8_t* b, size_t len, uint32_t* offsetOut) {
    if (len <= UPDATE_HEADER_SIZE || len > UPDATE_HEADER_SIZE + MAX_UPDATE_CHUNK) return -1;
    *offsetOut = get32(b);
    return (int)(len - UPDATE_HEADER_SIZE);
}

struct ListEntry {
    uint16_t index;
    uint8_t flags;
    uint32_t size;
    uint32_t epoch;
    uint32_t uptimeMs;
};

inline void packListEntry(const ListEntry& e, uint8_t out[LIST_ENTRY_SIZE]) {
    put16(out, e.index);
    out[2] = e.flags;
    out[3] = 0;
    put32(out + 4, e.size);
    put32(out + 8, e.epoch);
    put32(out + 12, e.uptimeMs);
}

inline void packFrameHeader(uint8_t kind, uint16_t seq, uint8_t out[FRAME_HEADER_SIZE]) {
    out[0] = kind;
    put16(out + 1, seq);
}

inline void packEnd(uint8_t op, uint8_t status, uint32_t totalLen, uint32_t crc,
                    uint8_t out[END_PAYLOAD_SIZE]) {
    out[0] = op;
    out[1] = status;
    put32(out + 2, totalLen);
    put32(out + 6, crc);
}

// Dating rule from docs/protocol.md §2, camera side: a photo from this boot
// whose clock was unset can be dated from uptime once the clock *is* set.
// Returns 0 if it can't be dated.
inline uint32_t estimateEpoch(uint32_t photoT, uint16_t photoBoot, uint32_t photoUp,
                              uint16_t bootNow, uint32_t uptimeNow, uint32_t epochNow,
                              uint8_t* flagsOut) {
    if (photoT) {
        if (flagsOut) *flagsOut |= ENTRY_EPOCH_FROM_CLOCK;
        return photoT;
    }
    if (epochNow && photoBoot == bootNow && uptimeNow >= photoUp) {
        if (flagsOut) *flagsOut |= ENTRY_EPOCH_IS_ESTIMATE;
        return epochNow - (uptimeNow - photoUp) / 1000;
    }
    return 0;
}

}  // namespace SyncProto
