#pragma once

#include <Arduino.h>
#include "ui.h"

namespace Display {

// Display dimensions
constexpr int WIDTH = 400;
constexpr int HEIGHT = 240;
constexpr int BYTES_PER_LINE = WIDTH / 8;  // 50 bytes

// The photo, dithered to 1-bit and cached so the save slide can re-blit it at a
// new offset each frame instead of re-dithering. Packed MSB-first, bit set =
// white, no padding.
constexpr int PHOTO_WIDTH = 320;
constexpr int PHOTO_HEIGHT = HEIGHT;
constexpr int PHOTO_BYTES = PHOTO_WIDTH / 8;  // 40 bytes per row

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
// One frame of the capture -> save slide. t=0 reproduces drawCapture(), t=1
// reproduces drawSave(). Reuses the photo dithered by the last drawCapture()
// call rather than re-dithering, so it must follow one.
void drawSaveTransition(float t);
// One frame of the save -> viewfinder slide out, same t convention. Bayer-
// dithers a live frame instead of reusing the capture, so the viewfinder can
// come back to life while the columns are still moving. Pass nullptr to keep
// showing whatever was last dithered.
void drawDismissTransition(const uint8_t* grayscale, int srcWidth, int srcHeight, float t);

// Gallery: one stored photo in the save-layout geometry (card flush left) with
// a browse column on the right: next (press) with the position counter, and
// back (hold). `bits` is panel polarity — set bit = white — and exactly
// PHOTO_BYTES * PHOTO_HEIGHT bytes, i.e. what Storage::loadPhoto() produces.
// Display knows nothing about files, which is what lets the preview tool hand
// it any bitmap. index is 0-based, total >= 1.
void drawGallery(const uint8_t* bits, int index, int total);
// Gallery with nothing to show: an empty card and only the back button.
void drawGalleryEmpty();

// True while a toast is on screen and not yet expired. Static screens (the
// gallery) redraw only when this changes; the viewfinder redraws every frame
// anyway.
bool toastVisible();

// The cached 1-bit photo, PHOTO_BYTES * PHOTO_HEIGHT bytes. This is the exact
// bitmap on the panel, so saving it can't disagree with the frame the user
// approved. Only valid until the next draw call re-dithers — in practice that
// means read it while the save screen is up.
const uint8_t* photoBits();
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
void setPhotoCount(int count);        // Photos on flash (not rendered yet)
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
