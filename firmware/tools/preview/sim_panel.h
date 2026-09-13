#pragma once
//
// Software model of the Sharp Memory LCD (LS027B7DH01). Consumes the SPI byte
// stream and CS/DISP pin transitions, reconstructs what the panel would be
// showing, and can dump it to a BMP.

#include <stdint.h>

namespace SimPanel {

constexpr int WIDTH = 400;
constexpr int HEIGHT = 240;

// Wire the model to the pins display.cpp was initialised with, so CS and DISP
// transitions can be told apart from any other GPIO traffic.
void attach(uint8_t csPin, uint8_t dispPin);

void csChanged(bool high);
void byteOut(uint8_t b);
void setClockHz(uint32_t hz);

// One byte per pixel, 0 = black, 1 = white.
const uint8_t* pixels();

// DISP low means the panel blanks regardless of memory contents — the single
// most common "why is the screen dead" cause on this carrier.
bool displayEnabled();

// Number of VCOM polarity flips seen so far. A run of frames with this stuck
// at a constant value is the burn-in bug.
uint32_t vcomToggleCount();

// Counts of malformed traffic (bad line address, short line, unknown command).
uint32_t protocolErrors();

// scale must be >= 1; the panel is small and 1:1 is unreadable on a hidpi
// screen. Returns false if the file could not be written.
bool writeBMP(const char* path, int scale);

}  // namespace SimPanel
