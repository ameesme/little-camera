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

int main() {
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
