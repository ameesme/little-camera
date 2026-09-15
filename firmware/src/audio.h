#pragma once

#include <Arduino.h>

namespace Audio {

enum class Melody {
    TaDaDa,
    DaDaTa,  // Reverse ta-da-da (descending) for sleep
    ChirpChirp,
    Random
};

void init(uint8_t buzzerPin);
void playClick(bool light = false);
// The wake-up sound: ascending ta-da-da, blocking (~250ms), bit-banged on
// the pin. Used right after a wake, where the LEDC tone task's state after
// light sleep is nothing to depend on and loop() isn't pumping update() yet.
void playWake();
void playMelody(Melody type = Melody::Random);
void update();
void stop();
bool isPlaying();

}
