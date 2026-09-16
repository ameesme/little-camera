#pragma once

#include <Arduino.h>

#include "chirp.h"

namespace Audio {

enum class Melody {
    TaDaDa,
    DaDaTa,  // Reverse ta-da-da (descending) for sleep
    ChirpChirp,
    Saved,   // A, A an octave up, E: the photo is on the flash
    Random
};

void init(uint8_t buzzerPin);
void playClick(bool light = false);

// A composed chirp (chirp.h, docs/mood.md). Blocking and bit-banged like the
// click, so it is heard straight out of light sleep; at most
// Chirp::MAX_TOTAL_MS plus one wave period per segment. Cancels a melody in
// flight — the pin is ours for the duration.
void playChirp(const Chirp::Score& score);
void playMelody(Melody type = Melody::Random);
void update();
void stop();
bool isPlaying();

}
