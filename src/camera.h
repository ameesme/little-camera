#pragma once

#include <Arduino.h>

namespace Camera {

// Camera frame dimensions (QVGA)
constexpr int WIDTH = 320;
constexpr int HEIGHT = 240;

bool init();
uint8_t* capture();  // Returns pointer to grayscale framebuffer, or nullptr on failure

}
