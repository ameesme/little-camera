#include "audio.h"

namespace Audio {

static uint8_t _buzzerPin = 0;

struct MelodyData {
    const uint16_t* freqs;
    const uint16_t* durs;
    const uint16_t* gaps;
    int len;
    bool noisy;
};

// Ta-da-da (ascending A major)
static constexpr uint16_t MELODY0_FREQS[] = {880, 1109, 1319};
static constexpr uint16_t MELODY0_DURS[] = {30, 30, 30};
static constexpr uint16_t MELODY0_GAPS[] = {150, 30, 30};

// Da-da-ta (descending A major — reverse of ta-da-da for sleep)
static constexpr uint16_t MELODY1_FREQS[] = {1319, 1109, 880};
static constexpr uint16_t MELODY1_DURS[] = {30, 30, 30};
static constexpr uint16_t MELODY1_GAPS[] = {150, 30, 30};

// Chirp-chirp (noisy ascending sweeps)
static constexpr uint16_t MELODY2_FREQS[] = {1500, 2500};
static constexpr uint16_t MELODY2_DURS[] = {0, 0};
static constexpr uint16_t MELODY2_GAPS[] = {50, 50};

// Saved (A5, A6, E6 — same tempo as ta-da-da)
static constexpr uint16_t MELODY3_FREQS[] = {880, 1760, 1319};
static constexpr uint16_t MELODY3_DURS[] = {30, 30, 30};
static constexpr uint16_t MELODY3_GAPS[] = {150, 30, 30};

static const MelodyData MELODIES[] = {
    {MELODY0_FREQS, MELODY0_DURS, MELODY0_GAPS, 3, false},  // TaDaDa
    {MELODY1_FREQS, MELODY1_DURS, MELODY1_GAPS, 3, false},  // DaDaTa
    {MELODY2_FREQS, MELODY2_DURS, MELODY2_GAPS, 2, true},   // ChirpChirp
    {MELODY3_FREQS, MELODY3_DURS, MELODY3_GAPS, 3, false},  // Saved
};
static constexpr int NUM_MELODIES = 2;  // Random picks from TaDaDa and ChirpChirp only

// Async melody state
static int currentMelody = 0;
static int melodyStep = -1;
static uint32_t melodyStepStart = 0;
static bool noteStarted = false;

static void legacySweep(uint16_t startFreq) {
    for (int f = startFreq; f < startFreq + 1500; f += 200) {
        tone(_buzzerPin, f);
        delayMicroseconds(600);
    }
    noTone(_buzzerPin);
}

void init(uint8_t buzzerPin) {
    _buzzerPin = buzzerPin;
    ledcDetachPin(_buzzerPin);
    pinMode(_buzzerPin, OUTPUT);
    digitalWrite(_buzzerPin, LOW);
    melodyStep = -1;
    noteStarted = false;
}

// A square wave for `us` microseconds, driven by digitalWrite alone. Used for
// the click instead of tone(): the click is a handful of sub-millisecond
// steps, and after a wake the LEDC tone task produced nothing for it while a
// bit-banged wave was heard every time. Blocking, but the whole click is
// under 10ms.
static void square(uint32_t freq, uint32_t us) {
    const uint32_t half = 500000UL / freq;
    for (uint32_t t = 0; t < us; t += 2 * half) {
        digitalWrite(_buzzerPin, HIGH);
        delayMicroseconds(half);
        digitalWrite(_buzzerPin, LOW);
        delayMicroseconds(half);
    }
}

void playClick(bool light) {
    // Same sweep and step timing as the original tone()-based click, but no
    // two clicks are quite alike: every frequency in the sweep is scaled by
    // a random 85-115%, which shifts the whole click brighter or duller
    // while keeping its shape. A mechanical shutter never sounds the same
    // twice either.
    ledcDetachPin(_buzzerPin);
    pinMode(_buzzerPin, OUTPUT);
    if (light) {
        // The light tick is 6ms of 5-9kHz, where the piezo's own resonance
        // flattens pitch differences, so it needs a wider spread than the
        // shutter click to be heard to vary — and its step length varies too,
        // which reads as a slightly longer or shorter tick.
        // Range sits below the original pitch: 100% was already the bright
        // end of what sounds good on this piezo.
        const uint32_t scale = (uint32_t)random(75, 101);
        const uint32_t step = (uint32_t)random(550, 900);
        for (int f = 8000; f > 4500; f -= 400) square((uint32_t)f * scale / 100, step);
    } else {
        const uint32_t scale = (uint32_t)random(85, 116);
        for (int f = 4000; f > 800; f -= 400) square((uint32_t)f * scale / 100, 800);
    }
    digitalWrite(_buzzerPin, LOW);
}

// Output policy for Chirp::play(): the pin, and a wait that busy-waits the
// sub-millisecond half-periods but yields (delay) for the inter-segment gaps,
// so the idle and BLE tasks get the silence even if not the sound. Chirps use
// chirp.h's own xorshift rather than random() so a score renders identically
// on the host — don't "unify" the two.
struct PinOut {
    void set(bool high) { digitalWrite(_buzzerPin, high ? HIGH : LOW); }
    void wait(uint32_t us) {
        if (us >= 2000) {
            delay(us / 1000);
            us %= 1000;
        }
        if (us) delayMicroseconds(us);
    }
};

void playChirp(const Chirp::Score& score) {
    if (melodyStep >= 0) {
        noTone(_buzzerPin);
        melodyStep = -1;
        noteStarted = false;
    }
    ledcDetachPin(_buzzerPin);
    pinMode(_buzzerPin, OUTPUT);
    PinOut out;
    Chirp::play(score, out);
    digitalWrite(_buzzerPin, LOW);
}

void playMelody(Melody type) {
    if (melodyStep >= 0) return;  // Already playing

    if (type == Melody::Random) {
        // Random picks TaDaDa (0) or ChirpChirp (2), skipping DaDaTa (sleep melody)
        currentMelody = random(2) == 0 ? 0 : 2;
    } else {
        currentMelody = static_cast<int>(type);
    }
    melodyStep = 0;
    melodyStepStart = millis();
    noteStarted = false;
}

void update() {
    if (melodyStep < 0) return;

    const MelodyData& m = MELODIES[currentMelody];
    uint32_t elapsed = millis() - melodyStepStart;
    uint16_t gap = m.gaps[melodyStep];

    if (elapsed >= gap && !noteStarted) {
        if (m.noisy) {
            legacySweep(m.freqs[melodyStep]);
        } else {
            tone(_buzzerPin, m.freqs[melodyStep], m.durs[melodyStep]);
        }
        noteStarted = true;
    }

    uint16_t noteDur = m.noisy ? 10 : m.durs[melodyStep];
    if (elapsed >= gap + noteDur) {
        melodyStep++;
        if (melodyStep >= m.len) {
            melodyStep = -1;
        } else {
            melodyStepStart = millis();
            noteStarted = false;
        }
    }
}

void stop() {
    noTone(_buzzerPin);
    melodyStep = -1;
    noteStarted = false;
}

bool isPlaying() {
    return melodyStep >= 0;
}

}
