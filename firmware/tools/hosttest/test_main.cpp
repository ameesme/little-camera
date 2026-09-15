// Host-side unit tests for the firmware's Arduino-free headers: the PBM header
// codec, CRC-32, SHA-256 and the short-code derivation. Plain C++, no
// framework — `make test` compiles and runs it. The target toolchain is not
// needed, which is the point: these are the pieces that have to agree byte for
// byte with the phone and the server, so they get checked on every change.

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../src/pbm_header.h"
#include "../../src/crc32.h"
#include "../../src/sha256.h"
#include "../../src/shortcode.h"
#include "../../src/sync_protocol.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void testCrc32() {
    const char* s = "123456789";
    CHECK(Crc32::of((const uint8_t*)s, 9) == 0xCBF43926u);
    // Incremental equals one-shot
    uint32_t c = Crc32::INIT;
    c = Crc32::update(c, (const uint8_t*)s, 4);
    c = Crc32::update(c, (const uint8_t*)s + 4, 5);
    CHECK(Crc32::finish(c) == 0xCBF43926u);
    CHECK(Crc32::of(nullptr, 0) == 0);
}

static void hex(const uint8_t* d, size_t n, char* out) {
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

static void testSha256() {
    uint8_t h[32];
    char s[65];
    Sha256::hash((const uint8_t*)"", 0, h);
    hex(h, 32, s);
    CHECK(!strcmp(s, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    Sha256::hash((const uint8_t*)"abc", 3, h);
    hex(h, 32, s);
    CHECK(!strcmp(s, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    // 56 bytes: exercises the two-block padding path
    const char* m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Sha256::hash((const uint8_t*)m, strlen(m), h);
    hex(h, 32, s);
    CHECK(!strcmp(s, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

static void testShortCode() {
    // Vectors from docs/protocol.md §1
    struct { const char* id; const char* code; } v[] = {
        {"7cdfa1e2b3c4", "MM48F3"},
        {"000000000000", "882TLC"},
        {"ffffffffffff", "XMPHSF"},
        {"a1b2c3d4e5f6", "ZZWB7E"},
    };
    for (auto& t : v) {
        char out[7];
        ShortCode::compute(t.id, out);
        CHECK(!strcmp(out, t.code));
        if (strcmp(out, t.code)) fprintf(stderr, "  %s -> %s, want %s\n", t.id, out, t.code);
    }
}

static void testPbmHeader() {
    // Old firmware: no comment line
    const char* old = "P4\n320 240\n";
    Pbm::Header h;
    CHECK(Pbm::parseHeader((const uint8_t*)old, strlen(old) + 1, &h));
    CHECK(h.width == 320 && h.height == 240);
    CHECK(!h.hasMeta && h.t == 0 && h.up == 0 && h.boot == 0);
    CHECK(h.rasterOffset == 11);
    CHECK(Pbm::rasterBytes(h) == 9600);

    // New firmware: comment line
    char buf[Pbm::MAX_HEADER];
    int n = Pbm::writeHeader(buf, sizeof(buf), 320, 240, 17, 48213, 1757789000u);
    CHECK(n > 0);
    CHECK(!strcmp(buf, "P4\n# boot=17 up=48213 t=1757789000\n320 240\n"));
    Pbm::Header h2;
    CHECK(Pbm::parseHeader((const uint8_t*)buf, (size_t)n + 1, &h2));
    CHECK(h2.hasMeta && h2.boot == 17 && h2.up == 48213 && h2.t == 1757789000u);
    CHECK(h2.width == 320 && h2.height == 240 && h2.rasterOffset == (size_t)n);

    // Worst-case lengths fit MAX_HEADER
    n = Pbm::writeHeader(buf, sizeof(buf), 65535, 65535, 65535, 4294967295u, 4294967295u);
    CHECK(n > 0 && (size_t)n < Pbm::MAX_HEADER);

    // Unknown keys are ignored, comments anywhere between tokens are fine
    const char* odd = "P4\n#hello\n# foo=1 t=99\n320\n# x\n240\n";
    Pbm::Header h3;
    CHECK(Pbm::parseHeader((const uint8_t*)odd, strlen(odd) + 1, &h3));
    CHECK(h3.width == 320 && h3.height == 240 && h3.t == 99);

    // Rejections
    CHECK(!Pbm::parseHeader((const uint8_t*)"P5\n320 240\n", 11, &h));
    CHECK(!Pbm::parseHeader((const uint8_t*)"P4\n320", 6, &h));           // truncated
    CHECK(!Pbm::parseHeader((const uint8_t*)"P4\n320 240\n", 10, &h));    // no raster byte within len
}

static void testSyncProtocol() {
    using namespace SyncProto;
    // Layouts pinned by docs/protocol.md §3
    CHECK(INFO_SIZE == 25 && LIST_ENTRY_SIZE == 16 && FRAME_HEADER_SIZE == 3 && END_PAYLOAD_SIZE == 10);
    CHECK(MAX_CHUNK == 506);

    Info i;
    const uint8_t mac[6] = {0x7c, 0xdf, 0xa1, 0xe2, 0xb3, 0xc4};
    memcpy(i.mac, mac, 6);
    i.photoCount = 12; i.unsyncedCount = 3; i.newestIndex = 57; i.boot = 17;
    i.uptimeMs = 0x01020304; i.epoch = 1757789000u; i.flags = INFO_TIME_VALID | INFO_STORAGE_OK;
    uint8_t p[INFO_SIZE];
    packInfo(i, p);
    CHECK(p[0] == 1);
    CHECK(!memcmp(p + 1, mac, 6));
    CHECK(get16(p + 7) == 12 && get16(p + 9) == 3 && get16(p + 11) == 57 && get16(p + 13) == 17);
    CHECK(p[15] == 0x04 && p[16] == 0x03 && p[17] == 0x02 && p[18] == 0x01);  // little-endian
    CHECK(get32(p + 19) == 1757789000u);
    CHECK(p[23] == 0x03 && p[24] == 0);

    ListEntry e;
    e.index = 7; e.flags = ENTRY_SYNCED; e.size = 9646; e.epoch = 0; e.uptimeMs = 48213;
    uint8_t le[LIST_ENTRY_SIZE];
    packListEntry(e, le);
    CHECK(get16(le) == 7 && le[2] == 1 && le[3] == 0 && get32(le + 4) == 9646 &&
          get32(le + 8) == 0 && get32(le + 12) == 48213);

    uint8_t fh[FRAME_HEADER_SIZE];
    packFrameHeader(KIND_PHOTO_DATA, 0x0201, fh);
    CHECK(fh[0] == 0x02 && fh[1] == 0x01 && fh[2] == 0x02);

    uint8_t end[END_PAYLOAD_SIZE];
    packEnd(OP_GET, STATUS_OK, 9646, 0xCBF43926u, end);
    CHECK(end[0] == OP_GET && end[1] == 0 && get32(end + 2) == 9646 && get32(end + 6) == 0xCBF43926u);

    // Dating rule: clock beats estimate, estimate needs same boot and a set clock
    uint8_t f = 0;
    CHECK(estimateEpoch(1000, 1, 5000, 1, 9000, 2000, &f) == 1000 && (f & ENTRY_EPOCH_FROM_CLOCK));
    f = 0;
    CHECK(estimateEpoch(0, 17, 48213, 17, 60213, 1757789000u, &f) == 1757789000u - 12 &&
          (f & ENTRY_EPOCH_IS_ESTIMATE));
    f = 0;
    CHECK(estimateEpoch(0, 16, 48213, 17, 60213, 1757789000u, &f) == 0 && f == 0);  // other boot
    CHECK(estimateEpoch(0, 17, 48213, 17, 60213, 0, &f) == 0);                        // clock unset
}

int main() {
    testSyncProtocol();
    testCrc32();
    testSha256();
    testShortCode();
    testPbmHeader();
    if (failures) {
        fprintf(stderr, "%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("ok: %d checks\n", checks);
    return 0;
}
