#pragma once

#include <Arduino.h>
#include "ui.h"

namespace Display {

// Display dimensions
constexpr int WIDTH = 400;
constexpr int HEIGHT = 240;
constexpr int BYTES_PER_LINE = WIDTH / 8;  // 50 bytes

void init(uint8_t sclk, uint8_t mosi, uint8_t cs, uint8_t disp);
void clear();
void fillPattern(uint8_t pattern);
void drawTestPattern();
void drawSplash();
void drawSleep();
void drawViewfinder(const uint8_t* grayscale, int srcWidth, int srcHeight);
void drawCapture(const uint8_t* grayscale, int srcWidth, int srcHeight);  // Floyd-Steinberg dither
// Post-capture save prompt: photo shifts left, send/trash buttons take the
// right column. Not the (future) gallery — this only acts on the frame you
// just shot.
void drawSave(const uint8_t* grayscale, int srcWidth, int srcHeight);
// VCOM must keep flipping or the panel accumulates DC bias and burns in. The
// datasheet wants >=1Hz; 5s is a deliberate tradeoff — the panel tolerates it
// and it sets how often we have to wake out of light sleep, which is the whole
// idle power budget. Shared by the awake loop and the sleep keep-alive so both
// paths can't drift apart.
constexpr uint32_t VCOM_INTERVAL_MS = 5000;

void refresh();     // Call periodically; toggles VCOM at most once per interval
void toggleVcom();  // Force a VCOM toggle now — used by the sleep keep-alive

// UI state for sidebar
void setBatteryPercent(int percent);  // 0-100
void setInboxCount(int count);
void setSignalLevel(UI::SignalLevel level);

// Toast positioning
enum class ToastHAlign { Left, Center, Right };
enum class ToastVAlign { Top, Bottom };

// Show a toast message that persists over viewfinder updates.
// duration_ms: how long to show (0 = indefinite until clearToast)
// inverted: true = white text on black background
void showToast(const char* text, ToastHAlign halign, ToastVAlign valign,
               bool inverted = false, uint32_t duration_ms = 2000);
void clearToast();

}
