#pragma once
//
// Host-build stand-in for SPI.h. Every byte handed to the "peripheral" is
// forwarded to the panel model, which decodes it exactly the way the
// LS027B7DH01 would. That means the preview exercises the real wire protocol
// — command bits, VCOM bit, line addressing, bit order — not just the
// framebuffer contents.

#include <stdint.h>
#include <stddef.h>
#include "sim_panel.h"

#define HSPI 2
#define VSPI 3
#define FSPI 1
#define SPI_MODE0 0

class SPIClass {
public:
    explicit SPIClass(int) {}

    void begin(int8_t, int8_t, int8_t, int8_t) {}
    void setFrequency(uint32_t hz) { SimPanel::setClockHz(hz); }
    void setDataMode(uint8_t) {}

    uint8_t transfer(uint8_t b) {
        SimPanel::byteOut(b);
        return 0;  // Panel is write-only; there is no MISO to read back.
    }

    void transferBytes(const uint8_t* out, uint8_t* /*in*/, size_t len) {
        for (size_t i = 0; i < len; i++) SimPanel::byteOut(out[i]);
    }
};
