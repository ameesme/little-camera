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
#include "../../src/chirp.h"
#include "../../src/mood.h"

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
    CHECK(INFO_SIZE == 32 && LIST_ENTRY_SIZE == 16 && FRAME_HEADER_SIZE == 3 && END_PAYLOAD_SIZE == 10);
    CHECK(UPDATE_STATUS_SIZE == 15 && MAX_CONTROL == 37 && UPDATE_HEADER_SIZE == 4);
    CHECK(MAX_CHUNK == 506 && MAX_UPDATE_CHUNK == 505);

    Info i;
    const uint8_t mac[6] = {0x7c, 0xdf, 0xa1, 0xe2, 0xb3, 0xc4};
    memcpy(i.mac, mac, 6);
    i.photoCount = 12; i.unsyncedCount = 3; i.newestIndex = 57; i.boot = 17;
    i.uptimeMs = 0x01020304; i.epoch = 1757789000u; i.flags = INFO_TIME_VALID | INFO_STORAGE_OK;
    i.fwMajor = 0; i.fwMinor = 2; i.fwPatch = 0;
    i.updateState = UPDATE_IDLE; i.updateSpace = 0x330000;
    uint8_t p[INFO_SIZE];
    packInfo(i, p);
    CHECK(p[0] == 2);
    CHECK(!memcmp(p + 1, mac, 6));
    CHECK(get16(p + 7) == 12 && get16(p + 9) == 3 && get16(p + 11) == 57 && get16(p + 13) == 17);
    CHECK(p[15] == 0x04 && p[16] == 0x03 && p[17] == 0x02 && p[18] == 0x01);  // little-endian
    CHECK(get32(p + 19) == 1757789000u);
    CHECK(p[23] == 0x03);
    CHECK(p[24] == 0 && p[25] == 2 && p[26] == 0 && p[27] == UPDATE_IDLE);
    CHECK(get32(p + 28) == 0x330000u);
    // The v1 prefix is untouched, which is what lets a reader parse by length.
    i.flags |= INFO_TRIAL;
    packInfo(i, p);
    CHECK(p[23] == (INFO_TIME_VALID | INFO_STORAGE_OK | INFO_TRIAL));

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


static void testUpdateProtocol() {
    using namespace SyncProto;

    // UPDATE_STATUS, docs/protocol.md §3.7
    UpdateStatus u;
    u.op = OP_UPDATE_BEGIN; u.status = STATUS_OK; u.state = UPDATE_RECEIVING;
    u.nextOffset = 0x00012345; u.total = 1481712; u.chunk = 245; u.window = 245 * UPDATE_SLOTS;
    uint8_t out[UPDATE_STATUS_SIZE];
    packUpdateStatus(u, out);
    CHECK(out[0] == 0x10 && out[1] == 0 && out[2] == 1);
    CHECK(get32(out + 3) == 0x00012345u && get32(out + 7) == 1481712u);
    CHECK(get16(out + 11) == 245 && get16(out + 13) == 245 * UPDATE_SLOTS);

    // UPDATE_BEGIN: opcode, size, digest — and nothing guessed at when short
    uint8_t cmd[MAX_CONTROL];
    cmd[0] = OP_UPDATE_BEGIN;
    put32(cmd + 1, 1481712);
    for (int k = 0; k < 32; k++) cmd[5 + k] = (uint8_t)(k * 7 + 1);
    uint32_t size = 0;
    uint8_t digest[32] = {0};
    CHECK(parseUpdateBegin(cmd, sizeof(cmd), &size, digest));
    CHECK(size == 1481712u && digest[0] == 1 && digest[31] == 31 * 7 + 1);
    CHECK(!parseUpdateBegin(cmd, sizeof(cmd) - 1, &size, digest));
    CHECK(!parseUpdateBegin(cmd, 1, &size, digest));

    // Update writes: [u32 offset][data]
    uint8_t w[UPDATE_HEADER_SIZE + 8];
    put32(w, 4096);
    uint32_t offset = 0;
    CHECK(parseUpdateWrite(w, sizeof(w), &offset) == 8 && offset == 4096);
    CHECK(parseUpdateWrite(w, UPDATE_HEADER_SIZE, &offset) == -1);      // header only, no data
    CHECK(parseUpdateWrite(w, 0, &offset) == -1);
    // A write longer than the biggest MTU allows is not truncated, it is refused
    CHECK(parseUpdateWrite(w, UPDATE_HEADER_SIZE + MAX_UPDATE_CHUNK + 1, &offset) == -1);

    // Streaming SHA-256 equals the one-shot, at every chunk boundary that a
    // 505-byte BLE chunk can land on.
    uint8_t blob[2048];
    for (size_t k = 0; k < sizeof(blob); k++) blob[k] = (uint8_t)(k * 31 + 7);
    uint8_t once[32], streamed[32];
    Sha256::hash(blob, sizeof(blob), once);
    const size_t steps[] = {1, 55, 64, 65, 245, 505};
    for (size_t step : steps) {
        Sha256::Ctx ctx;
        for (size_t at = 0; at < sizeof(blob); at += step) {
            const size_t n = (at + step <= sizeof(blob)) ? step : sizeof(blob) - at;
            Sha256::update(ctx, blob + at, n);
        }
        Sha256::final(ctx, streamed);
        CHECK(!memcmp(once, streamed, 32));
    }
}

// ---- chirp.h ---------------------------------------------------------------

static bool sameScore(const Chirp::Score& a, const Chirp::Score& b) {
    if (a.n != b.n || a.happiness != b.happiness || a.noiseSeed != b.noiseSeed) return false;
    for (uint8_t i = 0; i < a.n; i++) {
        const Chirp::Segment &x = a.seg[i], &y = b.seg[i];
        if (x.kind != y.kind || x.f0 != y.f0 || x.f1 != y.f1 || x.ms != y.ms || x.gapMs != y.gapMs) return false;
    }
    return true;
}

// Output policy that only accumulates time: measures what play() really takes.
struct TimingOut {
    uint64_t us = 0;
    uint32_t toggles = 0;
    void set(bool) { toggles++; }
    void wait(uint32_t u) { us += u; }
};

static void testChirp() {
    // Detune arithmetic
    CHECK(Chirp::detune(880, 0) == 880);
    CHECK(Chirp::detune(880, 100) == 932);    // A#5 = 932.3
    CHECK(Chirp::detune(1760, 50) == 1812);   // 1811.5
    CHECK(Chirp::detune(3520, 100) == Chirp::F_MAX);  // 3729.7, clamped to the ceiling
    CHECK(Chirp::detune(880, -100) == 831);   // 830.6
    CHECK(Chirp::detune(880, 5) >= 882 && Chirp::detune(880, 5) <= 884);
    CHECK(Chirp::detune(880, 300) == Chirp::detune(880, 100));  // Clamped
    CHECK(Chirp::onScale(1319) && !Chirp::onScale(1320));

    // Rng
    Chirp::Rng z(0);
    CHECK(z.next() != 0);
    for (int i = 0; i < 100; i++) CHECK(z.below(7) < 7);

    // Determinism: same seed, same score
    for (uint32_t seed = 1; seed < 20; seed++) {
        Chirp::Rng a(seed), b(seed);
        CHECK(sameScore(Chirp::compose(100, a), Chirp::compose(100, b)));
    }

    // Variety at full happiness
    {
        uint32_t firsts[200];
        int distinct = 0;
        bool two = false, three = false;
        for (uint32_t seed = 1; seed <= 200; seed++) {
            Chirp::Rng r(seed);
            Chirp::Score s = Chirp::compose(255, r);
            uint32_t key = ((uint32_t)s.seg[0].f0 << 16) | s.seg[0].ms;
            bool seen = false;
            for (int i = 0; i < distinct; i++) if (firsts[i] == key) seen = true;
            if (!seen) firsts[distinct++] = key;
            if (s.n == 2) two = true;
            if (s.n == 3) three = true;
        }
        CHECK(distinct >= 20);
        CHECK(two && three);
    }

    // Invariants across every band
    const uint8_t levels[] = {0, 63, 64, 127, 128, 191, 192, 255};
    for (uint8_t h : levels) {
        bool sawNoise = false, sawOffScale = false;
        uint32_t pitched = 0, offScale = 0, sweeps = 0, rising = 0;
        for (uint32_t seed = 1; seed <= 2000; seed++) {
            Chirp::Rng r(seed * 2654435761u + h);
            Chirp::Score s = Chirp::compose(h, r);
            CHECK(s.n >= 2 && s.n <= Chirp::MAX_SEGMENTS);  // Never a single beep
            CHECK(Chirp::totalMs(s) <= Chirp::MAX_TOTAL_MS);
            CHECK(Chirp::totalMs(s) >= Chirp::MIN_TOTAL_MS);
            CHECK(s.happiness == h);
            bool anyPitched = false;
            for (uint8_t i = 0; i < s.n; i++) {
                const Chirp::Segment& g = s.seg[i];
                CHECK(g.ms >= Chirp::MIN_SEG_MS && g.ms <= Chirp::MAX_SEG_MS);
                if (i == s.n - 1) CHECK(g.gapMs == 0);
                else CHECK(g.gapMs >= Chirp::BANDS[Chirp::band(h)].gapMin);
                switch (g.kind) {
                    case Chirp::Kind::Tone:
                        CHECK(g.f0 >= Chirp::F_MIN && g.f0 <= Chirp::F_MAX && g.f1 == 0);
                        anyPitched = true;
                        pitched++;
                        if (!Chirp::onScale(g.f0)) { offScale++; sawOffScale = true; }
                        break;
                    case Chirp::Kind::Sweep:
                        CHECK(g.f0 >= Chirp::F_MIN && g.f0 <= Chirp::F_MAX);
                        CHECK(g.f1 >= Chirp::F_MIN && g.f1 <= Chirp::F_MAX);
                        CHECK(g.f0 != g.f1);
                        anyPitched = true;
                        pitched += 2;
                        sweeps++;
                        if (g.f1 > g.f0) rising++;
                        if (!Chirp::onScale(g.f0)) { offScale++; sawOffScale = true; }
                        if (!Chirp::onScale(g.f1)) { offScale++; sawOffScale = true; }
                        break;
                    case Chirp::Kind::Noise:
                        CHECK(g.f0 >= 6000 && g.f0 <= 40000 && g.f1 == 0);
                        CHECK(g.ms <= Chirp::NOISE_MAX_MS);
                        sawNoise = true;
                        break;
                }
            }
            CHECK(anyPitched);
            char desc[64];
            CHECK(Chirp::describe(s, desc, sizeof(desc)) < 64);
            CHECK(strlen(desc) > 0);

            // Played duration: whole periods overrun by < 1 period per segment
            TimingOut out;
            Chirp::play(s, out);
            CHECK(out.us <= (uint64_t)Chirp::MAX_TOTAL_MS * 1000 + (uint64_t)s.n * 1250);
            CHECK(out.us >= (uint64_t)Chirp::totalMs(s) * 1000);
            CHECK(out.toggles > 0);
        }
        if (Chirp::band(h) == 3) {
            CHECK(!sawNoise);
            CHECK(!sawOffScale);
            CHECK(sweeps == 0 || rising * 10 >= sweeps * 7);
        }
        if (Chirp::band(h) == 0) {
            CHECK(sawNoise);
            CHECK(sawOffScale);
            CHECK(offScale * 2 > pitched);
        }
    }
}

// ---- mood.h ----------------------------------------------------------------

static const uint32_t H = 3600;

// Runs the mood machine at 5s ticks (the VCOM cadence) from `from` to `to`
// seconds of uptime, playing whatever is due; returns the number of chirps
// and records their times.
struct Sim {
    Mood::State s;
    Mood::Clock c;
    Chirp::Rng rng;
    uint32_t times[512];
    Mood::Due kinds[512];
    int n = 0;
    Sim(uint32_t seed) : rng(seed) { s = Mood::State(); c.uptimeS = 0; c.epoch = 0; c.tzMin = 0; c.tzKnown = false; }
    void run(uint32_t to) {
        for (; c.uptimeS <= to; c.uptimeS += 5) {
            if (c.epoch) c.epoch += 5;
            Mood::Due d = Mood::poll(s, c, rng);
            if (d != Mood::Due::None) {
                if (n < 512) { times[n] = c.uptimeS; kinds[n] = d; }
                n++;
                Mood::onChirpPlayed(s, c, d, rng);
            }
        }
    }
};

static void testMood() {
    // Curve
    CHECK(Mood::happinessFor(0) == 255);
    CHECK(Mood::happinessFor(84 * H) == 128 || Mood::happinessFor(84 * H) == 127);
    CHECK(Mood::happinessFor(Mood::WEEK_S) == 0);
    CHECK(Mood::happinessFor(Mood::WEEK_S * 3) == 0);
    CHECK(Mood::happinessFor(24 * H) == 219);  // 255 - 36.4
    CHECK(Chirp::band(Mood::happinessFor(3 * 24 * H)) == 2);
    CHECK(Chirp::band(Mood::happinessFor(6 * 24 * H)) == 0);

    // A month without a photo, no wall clock
    for (uint32_t seed = 1; seed <= 20; seed++) {
        Sim sim(seed);
        Mood::onPhoto(sim.s, sim.c, sim.rng);
        CHECK(Mood::happiness(sim.s, sim.c) == 255);
        sim.run(30 * 24 * H);
        CHECK(sim.n >= 2 && sim.n <= 512);
        // The one-hour chirp, once, in the window
        CHECK(sim.kinds[0] == Mood::Due::HourChirp);
        CHECK(sim.times[0] >= Mood::HOUR_CHIRP_MIN_S && sim.times[0] <= Mood::HOUR_CHIRP_MAX_S + 5);
        int hourChirps = 0;
        for (int i = 0; i < sim.n; i++) if (sim.kinds[i] == Mood::Due::HourChirp) hourChirps++;
        CHECK(hourChirps == 1);
        // Nothing else before 24h; first attention within 8h after; then 12-20h apart
        CHECK(sim.n >= 2);
        CHECK(sim.times[1] >= Mood::ATTENTION_AFTER_S);
        CHECK(sim.times[1] <= Mood::ATTENTION_AFTER_S + Mood::FIRST_INTERVAL_MAX_S + 10);
        for (int i = 2; i < sim.n; i++) {
            uint32_t gap = sim.times[i] - sim.times[i - 1];
            CHECK(gap >= Mood::INTERVAL_MIN_S && gap <= Mood::INTERVAL_MAX_S + 10);
        }
        // Never more than two in any 24h window
        for (int i = 1; i < sim.n; i++)
            for (int j = i + 2; j < sim.n; j++)
                if (sim.times[j] - sim.times[i] < 24 * H) CHECK(false);
        CHECK(Mood::happiness(sim.s, sim.c) == 0);
    }

    // Night rule with a clock and a timezone: nothing between 22:00 and 08:00 local,
    // deferred chirps land 08:00-10:00.
    for (uint32_t seed = 1; seed <= 10; seed++) {
        Sim sim(seed);
        sim.c.epoch = 1700000000;  // 2023-11-14 22:13:20 UTC
        sim.c.tzMin = 60;          // 23:13 local
        sim.c.tzKnown = true;
        Mood::onPhoto(sim.s, sim.c, sim.rng);
        sim.run(20 * 24 * H);
        CHECK(sim.n >= 2);
        // The one-hour chirp at ~00:13 local is night: swallowed, not deferred
        for (int i = 0; i < sim.n; i++) CHECK(sim.kinds[i] != Mood::Due::HourChirp);
        for (int i = 0; i < sim.n; i++) {
            uint32_t epoch = 1700000000 + sim.times[i];
            uint32_t sod = (uint32_t)(((int64_t)epoch + 3600) % 86400);
            uint32_t hour = sod / 3600;
            CHECK(hour >= 8 && hour < 22);
        }
        for (int i = 1; i < sim.n; i++)
            for (int j = i + 2; j < sim.n; j++)
                if (sim.times[j] - sim.times[i] < 24 * H) CHECK(false);
    }
    // The one-hour chirp survives when it falls in daytime
    {
        Sim sim(3);
        sim.c.epoch = 1700000000 + 12 * H;  // 11:13 local
        sim.c.tzMin = 60; sim.c.tzKnown = true;
        Mood::onPhoto(sim.s, sim.c, sim.rng);
        sim.run(2 * H);
        CHECK(sim.n == 1 && sim.kinds[0] == Mood::Due::HourChirp);
    }

    // Persist and resume without a clock: powered-off time does not count
    {
        Sim a(7);
        Mood::onPhoto(a.s, a.c, a.rng);
        a.run(10 * H);
        Mood::Persisted p = Mood::snapshot(a.s, a.c);
        CHECK(p.elapsedS >= 10 * H && p.elapsedS <= 10 * H + 5);
        Sim b(8);
        b.c.uptimeS = 3;  // Fresh boot, tiny uptime
        Mood::boot(b.s, b.c, &p, 0, b.rng);
        CHECK(Mood::elapsed(b.s, b.c) == p.elapsedS);
        CHECK(b.s.hourChirpAtS == 0);  // Already played before the reset
        // With a clock and a known photo epoch, the wall clock wins
        Sim c2(9);
        c2.c.epoch = 1700000000;
        Mood::onPhoto(c2.s, c2.c, c2.rng);
        Mood::Persisted p2 = Mood::snapshot(c2.s, c2.c);
        Sim d(10);
        d.c.uptimeS = 3;
        d.c.epoch = 1700000000 + 3 * 24 * H;  // Three days off
        Mood::boot(d.s, d.c, &p2, 0, d.rng);
        CHECK(Mood::elapsed(d.s, d.c) == 3 * 24 * H);
        CHECK(Chirp::band(Mood::happiness(d.s, d.c)) == 2);
    }
    // No state: seeded from the newest photo's header
    {
        Sim e(11);
        e.c.epoch = 1700000000 + 2 * 24 * H;
        Mood::boot(e.s, e.c, nullptr, 1700000000, e.rng);
        CHECK(Mood::elapsed(e.s, e.c) == 2 * 24 * H);
        CHECK(e.s.hourChirpAtS == 0);
        Sim f(12);
        Mood::boot(f.s, f.c, nullptr, 0, f.rng);  // Fresh flash: happy, hour chirp owed
        CHECK(Mood::elapsed(f.s, f.c) == 0 && f.s.hourChirpAtS != 0);
        Mood::Persisted stale = Mood::snapshot(f.s, f.c);
        stale.version = 99;
        Sim g(13);
        Mood::boot(g.s, g.c, &stale, 0, g.rng);  // Unknown version ignored
        CHECK(Mood::elapsed(g.s, g.c) == 0);
    }
    // Clock arrives later: the last photo's epoch is back-filled
    {
        Sim h(14);
        Mood::onPhoto(h.s, h.c, h.rng);
        CHECK(h.s.lastPhotoEpoch == 0);
        h.c.uptimeS = 5 * H;
        h.c.epoch = 1700000000;
        Mood::onClockSet(h.s, h.c);
        CHECK(h.s.lastPhotoEpoch == 1700000000 - 5 * H);
        h.c.uptimeS += 100;
        h.c.epoch += 100 + 24 * H;  // The wall clock jumped forward a day
        CHECK(Mood::elapsed(h.s, h.c) == 5 * H + 100 + 24 * H);
    }
    // Persist cadence
    {
        Sim k(15);
        Mood::onPhoto(k.s, k.c, k.rng);
        CHECK(!Mood::persistDue(k.s, k.c));
        k.c.uptimeS = Mood::PERSIST_EVERY_S;
        CHECK(Mood::persistDue(k.s, k.c));
        Mood::markPersisted(k.s, k.c);
        CHECK(!Mood::persistDue(k.s, k.c));
    }
}

int main() {
    testMood();
    testChirp();
    testSyncProtocol();
    testUpdateProtocol();
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
