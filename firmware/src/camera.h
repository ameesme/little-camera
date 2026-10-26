#pragma once

#include <Arduino.h>

namespace Camera {

// Camera frame dimensions (QVGA)
constexpr int WIDTH = 320;
constexpr int HEIGHT = 240;

bool init();         // Rails up, then the driver
void deinit();       // Driver down, then the rails — the sensor loses power
uint8_t* capture();  // Returns pointer to grayscale framebuffer, or nullptr on failure

}
