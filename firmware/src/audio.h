#pragma once

#include <Arduino.h>

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
void playMelody(Melody type = Melody::Random);
void update();
void stop();
bool isPlaying();

}
