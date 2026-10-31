#pragma once

#include <Arduino.h>

namespace Camera {

// Camera frame dimensions (QVGA)
constexpr int WIDTH = 320;
constexpr int HEIGHT = 240;

bool init();         // Rails up, then the driver
void deinit();       // Driver down, then the rails — the sensor loses power
// How the module sits in J2. Held across the deinit/init every sleep does, so a setting survives
// a doze; the console's `flip` is how it gets found on a board nobody has pointed at a wall yet.
void setFlip(bool vflip, bool hmirror);
bool flippedVertically();
bool mirrored();
uint8_t* capture();  // Returns pointer to grayscale framebuffer, or nullptr on failure

}
