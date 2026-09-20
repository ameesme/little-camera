#pragma once
// Compile-check stub: the preview shim is enough Arduino for the non-display
// firmware modules too. Nothing here runs; the `make check` target only asks
// the compiler whether the sources are well-formed, for when the target
// toolchain isn't at hand.
#include "../../preview/shim/Arduino.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Enough of the tone/LEDC HAL and random() for audio.cpp to syntax-check.
inline void tone(uint8_t, unsigned int, unsigned long = 0) {}
inline void noTone(uint8_t) {}
inline void ledcDetachPin(uint8_t) {}
inline long random(long) { return 0; }
inline long random(long, long hi) { return hi - 1; }
