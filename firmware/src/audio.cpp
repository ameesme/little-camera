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

static const MelodyData MELODIES[] = {
    {MELODY0_FREQS, MELODY0_DURS, MELODY0_GAPS, 3, false},  // TaDaDa
    {MELODY1_FREQS, MELODY1_DURS, MELODY1_GAPS, 3, false},  // DaDaTa
    {MELODY2_FREQS, MELODY2_DURS, MELODY2_GAPS, 2, true},   // ChirpChirp
};
static constexpr int NUM_MELODIES = 2;  // Random picks from TaDaDa and ChirpChirp only

// Async melody state
static int currentMelody = 0;
static int melodyStep = -1;
static uint32_t melodyStepStart = 0;
static bool noteStarted = false;

static void playChirp(uint16_t startFreq) {
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

void playClick(bool light) {
    if (light) {
        for (int f = 8000; f > 4500; f -= 400) {
            tone(_buzzerPin, f);
            delayMicroseconds(700);
        }
    } else {
        for (int f = 4000; f > 800; f -= 400) {
            tone(_buzzerPin, f);
            delayMicroseconds(800);
        }
    }
    noTone(_buzzerPin);
}

void playWake() {
    // Same three notes as the boot melody, but driven by digitalWrite() and
    // delayMicroseconds() alone: no tone task, no LEDC channel, no dependence
    // on the peripheral state a light sleep leaves behind.
    ledcDetachPin(_buzzerPin);
    pinMode(_buzzerPin, OUTPUT);
    for (int i = 0; i < 3; i++) {
        const uint32_t freq = MELODY0_FREQS[i];
        const uint32_t half = 500000UL / freq;   // Half period, microseconds
        const uint32_t cycles = freq * 60 / 1000;  // 60ms per note
        for (uint32_t c = 0; c < cycles; c++) {
            digitalWrite(_buzzerPin, HIGH);
            delayMicroseconds(half);
            digitalWrite(_buzzerPin, LOW);
            delayMicroseconds(half);
        }
        delay(30);
    }
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
            playChirp(m.freqs[melodyStep]);
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
