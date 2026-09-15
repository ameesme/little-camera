#pragma once

// Procedural chirps: a happiness level in, a short score out, and a player
// that turns the score into pin toggles. Behaviour in docs/mood.md.
//
// Arduino-free on purpose: tools/hosttest checks the invariants (length cap,
// pitch range, on-scale when happy) and tools/chirp renders scores to WAV so
// they can be auditioned on a laptop. The player is a template over an output
// policy for the same reason — firmware, WAV tool and tests run the *same*
// timing loops, so what you hear on the laptop is what the pin does.
//
// Everything is integer arithmetic; the target has an FPU but the melodies and
// the click don't use it and this shouldn't be the first thing that does.
// C++11 only, like the sibling headers: platformio.ini does not pin -std.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

namespace Chirp {

constexpr uint32_t MAX_TOTAL_MS = 300;  // Hard ceiling incl. gaps; ta-da-da is exactly this
constexpr uint16_t MIN_SEG_MS = 15;
constexpr uint16_t MAX_SEG_MS = 120;
constexpr uint16_t NOISE_MAX_MS = 60;  // A longer hiss reads as a fault, not a mood
constexpr uint8_t MAX_SEGMENTS = 3;

// A5 -100 cents .. A7 +100 cents: the only pitches play() may ever see.
constexpr uint16_t F_MIN = 830;
constexpr uint16_t F_MAX = 3730;

// A major, A5..A7, rounded the way the melodies already round (1109, 1319).
constexpr uint8_t SCALE_LEN = 15;
constexpr uint16_t SCALE[SCALE_LEN] = {880,  988,  1109, 1175, 1319, 1480, 1661, 1760,
                                       1976, 2217, 2349, 2637, 2960, 3322, 3520};

// xorshift32. Seeded from esp_random() on the target, from literals in tests.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0xA5A5A5A5u) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
    uint32_t below(uint32_t n) { return n ? next() % n : 0; }
    uint16_t between(uint16_t lo, uint16_t hi) {  // Inclusive
        return (uint16_t)(lo + below((uint32_t)hi - lo + 1));
    }
    bool chance(uint8_t pct) { return below(100) < pct; }
};

enum class Kind : uint8_t { Tone, Noise, Sweep };

struct Segment {
    Kind kind;
    uint16_t f0;     // Tone: pitch. Sweep: start. Noise: 1-bit clock in Hz (6000..40000)
    uint16_t f1;     // Sweep: end. Otherwise 0
    uint16_t ms;
    uint16_t gapMs;  // Silence after this segment; 0 on the last
};

struct Score {
    Segment seg[MAX_SEGMENTS];
    uint8_t n;
    uint8_t happiness;
    uint32_t noiseSeed;  // The noise bits come from here, so a Score fully determines the sound
};

// ---- Pitch helpers ---------------------------------------------------------

// 2^(c/1200) * 1024 for c = 0, 10, ..., 100 cents.
constexpr uint16_t CENTS_Q10[11] = {1024, 1030, 1036, 1042, 1048, 1054, 1060, 1066, 1072, 1079, 1085};

inline uint32_t centsMult(uint8_t c) {  // c <= 100; linear between table rows, error < 1 cent
    uint8_t i = c / 10, r = c % 10;
    uint32_t a = CENTS_Q10[i], b = CENTS_Q10[i < 10 ? i + 1 : 10];
    return a + (b - a) * r / 10;
}

inline uint16_t detune(uint16_t hz, int16_t cents) {
    if (cents > 100) cents = 100;
    if (cents < -100) cents = -100;
    uint32_t m = centsMult((uint8_t)(cents < 0 ? -cents : cents));
    uint32_t f = cents >= 0 ? ((uint32_t)hz * m + 512) >> 10 : (((uint32_t)hz << 10) + m / 2) / m;
    if (f < F_MIN) f = F_MIN;
    if (f > F_MAX) f = F_MAX;
    return (uint16_t)f;
}

inline bool onScale(uint16_t hz) {
    for (uint8_t i = 0; i < SCALE_LEN; i++)
        if (SCALE[i] == hz) return true;
    return false;
}

// Four bands over happiness 0..255; the face and the docs use the same split.
inline uint8_t band(uint8_t happiness) { return happiness >> 6; }

// ---- Character per band ----------------------------------------------------

struct Band {
    uint8_t pTone, pSweep;         // Percent; noise gets the rest
    uint8_t pOne, pTwo;            // Percent for n = 1, n = 2; n = 3 gets the rest
    uint8_t segMax;                // ms; sad notes drag, happy ones are snappy
    uint8_t gapMin, gapMax;        // ms between segments
    uint8_t detMin, detMax;        // Cents on every pitched endpoint, random sign
    uint8_t pSemitone;             // Percent: a whole semitone shove instead of the small detune
    uint8_t pUp;                   // Percent: contour rises (next note above, sweep upward)
    uint8_t idxLo, idxHi;          // SCALE index window for the first pitch
    uint8_t clkMinKHz, clkMaxKHz;  // Noise clock window
};

// Happy: tone-heavy, on the scale, upper half of the range where the piezo is
// loud, rising, two or three quick notes (the start window stops short of the
// top so a rising move has room before it reflects). Sad: lower and duller,
// long notes, wide gaps, detuned, half the notes a semitone off, falling,
// noisy. Band 3 never gets noise or detune — that is the band the tests pin
// to "scale only".
//                                 tone swp  n1  n2  segMax gap    detune  semi  up  idx   clk kHz
constexpr Band BANDS[4] = {
    /* 0 sad       0- 63 */ {25, 35, 35, 40, 120, 20, 80, 30, 100, 50, 15, 0, 8, 8, 16},
    /* 1 glum     64-127 */ {40, 30, 30, 45, 120, 15, 60, 15, 45, 25, 35, 2, 10, 14, 24},
    /* 2 content 128-191 */ {60, 30, 30, 45, 100, 10, 40, 0, 10, 0, 70, 3, 10, 20, 32},
    /* 3 happy   192-255 */ {75, 25, 20, 45, 80, 10, 40, 0, 0, 0, 85, 4, 11, 0, 0},
};

// ---- Composer --------------------------------------------------------------

namespace detail {

inline uint8_t moveIdx(uint8_t idx, uint8_t by, bool up) {
    int i = up ? (int)idx + by : (int)idx - by;
    if (i >= SCALE_LEN) i = 2 * (SCALE_LEN - 1) - i;  // Reflect off the ends
    if (i < 0) i = -i;
    if (i >= SCALE_LEN) i = SCALE_LEN - 1;
    return (uint8_t)i;
}

inline uint16_t pitch(const Band& b, uint8_t idx, Rng& rng) {
    uint16_t hz = SCALE[idx];
    if (rng.chance(b.pSemitone)) return detune(hz, rng.chance(b.pUp) ? 100 : -100);
    if (b.detMax == 0) return hz;
    uint16_t c = rng.between(b.detMin, b.detMax);
    if (c == 0) return hz;
    return detune(hz, rng.chance(50) ? (int16_t)c : -(int16_t)c);
}

}  // namespace detail

inline Score compose(uint8_t happiness, Rng& rng) {
    const Band& b = BANDS[band(happiness)];
    Score s;
    s.happiness = happiness;
    s.n = rng.chance(b.pOne) ? 1 : (rng.chance(b.pTwo) ? 2 : 3);
    s.noiseSeed = rng.next();

    uint8_t idx = rng.between(b.idxLo, b.idxHi);
    bool pitchedSeen = false;
    int32_t budget = (int32_t)MAX_TOTAL_MS;

    for (uint8_t i = 0; i < s.n; i++) {
        Segment& g = s.seg[i];
        const int rest = s.n - 1 - i;
        // Room the remaining segments need at their minimums, so the cap for
        // this one can never squeeze a later one below MIN_SEG_MS. Budgeting
        // by construction: no rescaling afterwards, no rounding surprises.
        const int32_t reserve = rest ? rest * (int32_t)MIN_SEG_MS + (rest - 1) * (int32_t)b.gapMin : 0;
        const int32_t gapLo = rest ? b.gapMin : 0;
        int32_t segCap = budget - gapLo - reserve;
        if (segCap > b.segMax) segCap = b.segMax;
        if (segCap < MIN_SEG_MS) segCap = MIN_SEG_MS;
        g.ms = rng.between(MIN_SEG_MS, (uint16_t)segCap);
        if (rest) {
            int32_t gapCap = budget - g.ms - reserve;
            if (gapCap > b.gapMax) gapCap = b.gapMax;
            if (gapCap < b.gapMin) gapCap = b.gapMin;
            g.gapMs = rng.between(b.gapMin, (uint16_t)gapCap);
        } else {
            g.gapMs = 0;
        }
        budget -= g.ms + g.gapMs;

        uint32_t r = rng.below(100);
        g.kind = r < b.pTone ? Kind::Tone : (r < (uint32_t)b.pTone + b.pSweep ? Kind::Sweep : Kind::Noise);
        // A chirp is never noise alone.
        if (g.kind == Kind::Noise && rest == 0 && !pitchedSeen) g.kind = Kind::Tone;

        switch (g.kind) {
            case Kind::Tone:
                g.f0 = detail::pitch(b, idx, rng);
                g.f1 = 0;
                {
                    // A motif moves; a reflection off the end of the scale
                    // that lands on the same note is turned into a step.
                    uint8_t next = detail::moveIdx(idx, (uint8_t)rng.between(1, 4), rng.chance(b.pUp));
                    if (next == idx) next = detail::moveIdx(idx, 1, idx < SCALE_LEN / 2);
                    idx = next;
                }
                pitchedSeen = true;
                break;
            case Kind::Sweep: {
                uint8_t target = detail::moveIdx(idx, (uint8_t)rng.between(2, 5), rng.chance(b.pUp));
                if (target == idx) target = detail::moveIdx(idx, 1, idx < SCALE_LEN / 2);
                g.f0 = detail::pitch(b, idx, rng);
                g.f1 = detail::pitch(b, target, rng);
                if (g.f1 == g.f0) g.f1 = detune(g.f0, g.f0 < 2000 ? 100 : -100);
                idx = target;
                pitchedSeen = true;
                break;
            }
            case Kind::Noise:
                g.f0 = (uint16_t)(1000u * rng.between(b.clkMinKHz, b.clkMaxKHz));
                g.f1 = 0;
                if (g.ms > NOISE_MAX_MS) g.ms = NOISE_MAX_MS;  // The budget stays spent; shorter only helps
                break;
        }
    }
    return s;
}

inline uint32_t totalMs(const Score& s) {
    uint32_t t = 0;
    for (uint8_t i = 0; i < s.n; i++) t += (uint32_t)s.seg[i].ms + s.seg[i].gapMs;
    return t;
}

// One line for Serial and the WAV tool: `T1319/57 +12 S1661>2637/80 +20 N24k/30`.
inline int describe(const Score& s, char* out, size_t cap) {
    size_t used = 0;
    for (uint8_t i = 0; i < s.n && used < cap; i++) {
        const Segment& g = s.seg[i];
        int w = 0;
        switch (g.kind) {
            case Kind::Tone: w = snprintf(out + used, cap - used, "%sT%u/%u", i ? " " : "", (unsigned)g.f0, (unsigned)g.ms); break;
            case Kind::Sweep:
                w = snprintf(out + used, cap - used, "%sS%u>%u/%u", i ? " " : "", (unsigned)g.f0, (unsigned)g.f1,
                             (unsigned)g.ms);
                break;
            case Kind::Noise:
                w = snprintf(out + used, cap - used, "%sN%uk/%u", i ? " " : "", (unsigned)(g.f0 / 1000), (unsigned)g.ms);
                break;
        }
        if (w < 0) break;
        used += (size_t)w;
        if (g.gapMs && used < cap) {
            w = snprintf(out + used, cap - used, " +%u", (unsigned)g.gapMs);
            if (w < 0) break;
            used += (size_t)w;
        }
    }
    if (used >= cap) used = cap - 1;
    out[used] = 0;
    return (int)used;
}

// ---- Player ----------------------------------------------------------------
//
// Out must provide: void set(bool high); void wait(uint32_t us);
// Every loop runs whole periods, so a segment overruns by at most one period
// (1.2ms at 830Hz), never truncates. Sub-100Hz and sub-1kHz-clock values are
// treated as silence rather than divided by; compose() never emits them.

namespace detail {

template <class Out>
inline void squareWave(Out& out, uint32_t hz, uint32_t us) {
    if (hz < 100) { out.wait(us); return; }
    const uint32_t half = 500000UL / hz;
    for (uint32_t t = 0; t < us; t += 2 * half) {
        out.set(true);
        out.wait(half);
        out.set(false);
        out.wait(half);
    }
}

// One frequency step per period: 15ms at 2kHz is 30 steps, 120ms at 1kHz is
// 120. Smoother than the stepped legacy sweep and only costs a division per
// cycle. |f1 - f0| * t stays below 2^31 (2900 * 120000).
template <class Out>
inline void sweepWave(Out& out, uint32_t f0, uint32_t f1, uint32_t us) {
    if (f0 < 100 || f1 < 100) { out.wait(us); return; }
    for (uint32_t t = 0; t < us;) {
        const int32_t f = (int32_t)f0 + (int32_t)((int64_t)((int32_t)f1 - (int32_t)f0) * (int32_t)t / (int32_t)us);
        const uint32_t half = 500000UL / (uint32_t)f;
        out.set(true);
        out.wait(half);
        out.set(false);
        out.wait(half);
        t += 2 * half;
    }
}

// Sample-and-hold 1-bit white noise — the NES / PC-speaker noise channel.
// Flat spectrum up to clock/2: at 16-32kHz the piezo's 2-5kHz resonance turns
// it into a "tss" hiss; below ~8kHz the same generator becomes a crackly buzz
// because the energy sits in the audible range. The bands pick the clock.
template <class Out>
inline void noiseWave(Out& out, uint32_t clockHz, uint32_t us, uint32_t seed) {
    if (clockHz < 1000) { out.wait(us); return; }
    Rng r(seed);
    const uint32_t tick = 1000000UL / clockHz;
    for (uint32_t t = 0; t < us; t += tick) {
        out.set(((r.next() >> 16) & 1) != 0);
        out.wait(tick);
    }
}

}  // namespace detail

template <class Out>
inline void play(const Score& s, Out& out) {
    for (uint8_t i = 0; i < s.n; i++) {
        const Segment& g = s.seg[i];
        const uint32_t us = (uint32_t)g.ms * 1000;
        switch (g.kind) {
            case Kind::Tone: detail::squareWave(out, g.f0, us); break;
            case Kind::Sweep: detail::sweepWave(out, g.f0, g.f1, us); break;
            case Kind::Noise: detail::noiseWave(out, g.f0, us, s.noiseSeed + i * 0x9E3779B9u); break;
        }
        out.set(false);
        if (g.gapMs) out.wait((uint32_t)g.gapMs * 1000);
    }
}

}  // namespace Chirp
