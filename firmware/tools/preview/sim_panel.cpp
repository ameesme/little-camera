#include "sim_panel.h"

#include <stdio.h>
#include <string.h>

namespace SimPanel {

namespace {

// 1 = white, matching the panel and display.cpp's setPixel convention.
uint8_t g_pixels[WIDTH * HEIGHT];

uint8_t g_csPin = 0xFF;
uint8_t g_dispPin = 0xFF;
bool g_csHigh = false;
bool g_dispHigh = false;
bool g_vcom = false;
uint32_t g_vcomToggles = 0;
uint32_t g_errors = 0;
uint32_t g_clockHz = 0;

enum class State {
    Idle,          // CS low, nothing being clocked
    Command,       // Next byte is the command
    LineAddr,      // Next byte is a line address (or the final 0x00 terminator)
    LinePixels,    // Collecting BYTES_PER_LINE of pixel data
    LineTrailer,   // Per-line trailing dummy byte
    Draining,      // Command needs no more data; swallow the rest
};

State g_state = State::Idle;
int g_line = 0;
int g_pixelIdx = 0;
uint8_t g_lineBuf[WIDTH / 8];

// The controller shifts out MSB-first, but the panel latches multi-bit *values*
// (command byte, line address) LSB-first. display.cpp pre-reverses those via
// sendByte(), so recovering the value here means reversing again.
uint8_t reverseBits(uint8_t b) {
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}

void fillAll(uint8_t value) {
    memset(g_pixels, value, sizeof(g_pixels));
}

void commitLine() {
    // Line addresses are 1-indexed.
    int row = g_line - 1;
    if (row < 0 || row >= HEIGHT) {
        g_errors++;
        return;
    }
    // Pixel bytes are sent raw: bit 7 is the leftmost pixel, and bit 7 is also
    // the first bit on the wire, so no reversal here. This asymmetry with the
    // address bytes is deliberate in display.cpp, not a bug.
    for (int x = 0; x < WIDTH; x++) {
        uint8_t byte = g_lineBuf[x / 8];
        g_pixels[row * WIDTH + x] = (byte & (0x80 >> (x % 8))) ? 1 : 0;
    }
}

}  // namespace

void attach(uint8_t csPin, uint8_t dispPin) {
    g_csPin = csPin;
    g_dispPin = dispPin;
    g_state = State::Idle;
    g_vcomToggles = 0;
    g_errors = 0;
    fillAll(1);
}

void setClockHz(uint32_t hz) { g_clockHz = hz; }

void csChanged(bool high) {
    if (high && !g_csHigh) {
        g_state = State::Command;  // Rising edge starts a transaction
    } else if (!high && g_csHigh) {
        if (g_state == State::LinePixels) g_errors++;  // Truncated mid-line
        g_state = State::Idle;
    }
    g_csHigh = high;
}

void byteOut(uint8_t b) {
    if (!g_csHigh) {
        // CS is active-HIGH on this panel; clocking with it low goes nowhere.
        g_errors++;
        return;
    }

    switch (g_state) {
        case State::Command: {
            uint8_t cmd = reverseBits(b);

            bool vcom = (cmd & 0x40) != 0;
            if (vcom != g_vcom) g_vcomToggles++;
            g_vcom = vcom;

            switch (cmd & 0x0F) {
                case 0x01:  // Write lines
                    g_state = State::LineAddr;
                    break;
                case 0x04:  // Clear memory
                    fillAll(1);
                    g_state = State::Draining;
                    break;
                case 0x00:  // VCOM toggle only
                    g_state = State::Draining;
                    break;
                default:
                    g_errors++;
                    g_state = State::Draining;
                    break;
            }
            break;
        }

        case State::LineAddr: {
            uint8_t addr = reverseBits(b);
            if (addr == 0) {
                // Address 0 is invalid, so it is the end-of-transaction dummy.
                g_state = State::Draining;
            } else {
                g_line = addr;
                g_pixelIdx = 0;
                g_state = State::LinePixels;
            }
            break;
        }

        case State::LinePixels: {
            g_lineBuf[g_pixelIdx++] = b;
            if (g_pixelIdx == WIDTH / 8) {
                commitLine();
                g_state = State::LineTrailer;
            }
            break;
        }

        case State::LineTrailer:
            g_state = State::LineAddr;
            break;

        case State::Draining:
        case State::Idle:
            break;
    }
}

const uint8_t* pixels() { return g_pixels; }
bool displayEnabled() { return g_dispHigh; }
uint32_t vcomToggleCount() { return g_vcomToggles; }
uint32_t protocolErrors() { return g_errors; }

bool writeBMP(const char* path, int scale) {
    if (scale < 1) scale = 1;

    const int w = WIDTH * scale;
    const int h = HEIGHT * scale;
    const int rowBytes = w * 3;
    const int padding = (4 - (rowBytes % 4)) % 4;
    const int imageSize = (rowBytes + padding) * h;
    const int fileSize = 54 + imageSize;

    FILE* f = fopen(path, "wb");
    if (!f) return false;

    uint8_t header[54] = {0};
    header[0] = 'B'; header[1] = 'M';
    header[2] = (uint8_t)(fileSize); header[3] = (uint8_t)(fileSize >> 8);
    header[4] = (uint8_t)(fileSize >> 16); header[5] = (uint8_t)(fileSize >> 24);
    header[10] = 54;                       // Pixel data offset
    header[14] = 40;                       // DIB header size
    header[18] = (uint8_t)(w); header[19] = (uint8_t)(w >> 8);
    header[20] = (uint8_t)(w >> 16); header[21] = (uint8_t)(w >> 24);
    header[22] = (uint8_t)(h); header[23] = (uint8_t)(h >> 8);
    header[24] = (uint8_t)(h >> 16); header[25] = (uint8_t)(h >> 24);
    header[26] = 1;                        // Planes
    header[28] = 24;                       // Bits per pixel
    header[34] = (uint8_t)(imageSize); header[35] = (uint8_t)(imageSize >> 8);
    header[36] = (uint8_t)(imageSize >> 16); header[37] = (uint8_t)(imageSize >> 24);
    fwrite(header, 1, sizeof(header), f);

    uint8_t pad[3] = {0, 0, 0};
    // BMP rows run bottom-up.
    for (int y = h - 1; y >= 0; y--) {
        int srcY = y / scale;
        for (int x = 0; x < w; x++) {
            int srcX = x / scale;
            uint8_t v = g_pixels[srcY * WIDTH + srcX] ? 0xFF : 0x00;
            uint8_t bgr[3] = {v, v, v};
            fwrite(bgr, 1, 3, f);
        }
        if (padding) fwrite(pad, 1, padding, f);
    }

    fclose(f);
    return true;
}

}  // namespace SimPanel

// Bridge from the Arduino shim's digitalWrite into the panel model. Lives here
// so it can see the pin assignments captured by attach().
namespace Sim {
void pinWrite(uint8_t pin, uint8_t value) {
    if (pin == SimPanel::g_csPin) {
        SimPanel::csChanged(value != 0);
    } else if (pin == SimPanel::g_dispPin) {
        SimPanel::g_dispHigh = (value != 0);
    }
}
}  // namespace Sim
