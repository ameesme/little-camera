#pragma once
//
// Host-build stand-in for Arduino.h, just large enough to compile src/display.cpp
// on a desktop. Only the symbols display.cpp actually touches are here — if a
// build fails with a missing Arduino symbol, add it rather than pulling in a
// full Arduino emulation.
//
// The clock is virtual (see Sim::setMillis). Rendering code reads millis() for
// toast expiry and VCOM rate-limiting, so a real wall clock would make previews
// non-reproducible.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2

// No flash/RAM split on the host — PROGMEM data is just data.
#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))

namespace Sim {

// Virtual clock. Advance it explicitly to preview time-dependent UI
// (toast expiry, animations) deterministically.
void setMillis(uint32_t ms);
void advanceMillis(uint32_t ms);
uint32_t nowMillis();

// Pin state, so the panel model can see CS and DISP.
void pinWrite(uint8_t pin, uint8_t value);

}  // namespace Sim

inline uint32_t millis() { return Sim::nowMillis(); }
inline uint32_t micros() { return Sim::nowMillis() * 1000; }

// Delays are no-ops: the point of the host build is to skip real time.
inline void delay(uint32_t) {}
inline void delayMicroseconds(uint32_t) {}

inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t pin, uint8_t value) { Sim::pinWrite(pin, value); }
inline int digitalRead(uint8_t) { return LOW; }

// Minimal Serial that goes to stderr, keeping stdout free for piped output.
struct SimSerial {
    void begin(unsigned long) {}
    template <typename... Args>
    void printf(const char* fmt, Args... args) { fprintf(stderr, fmt, args...); }
    void println(const char* s) { fprintf(stderr, "%s\n", s); }
    void print(const char* s) { fprintf(stderr, "%s", s); }
    void flush() { fflush(stderr); }
    // Nothing ever arrives on the host console.
    int available() { return 0; }
    int read() { return -1; }
};

extern SimSerial Serial;
