/**
 * little-camera on the Xiao_Shutter carrier (XIAO ESP32-S3 Sense).
 *
 * Viewfinder + capture + sleep app. Cellular is not on this carrier
 * revision, so no modem code. Camera comes from the Sense expansion
 * over the B2B connector.
 */

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "audio.h"
#include "camera.h"
#include "display.h"

// Sharp Memory LCD (LS027B7DH01 / Adafruit 4694)
constexpr uint8_t PIN_LCD_SCLK = 7;   // D8
constexpr uint8_t PIN_LCD_MOSI = 9;   // D10
constexpr uint8_t PIN_LCD_CS   = 44;  // D7 — ACTIVE HIGH
constexpr uint8_t PIN_LCD_DISP = 3;   // D2 — display enable

// Controls
constexpr uint8_t PIN_BUZZ    = 2;    // D1

// D0. The netlist intended active-LOW (COM->SHUTTER, NO->GND), but on this
// carrier the line idles at a solid LOW and goes HIGH on press — consistent
// with the D2F's NC terminal being routed instead of NO. So: released = LOW
// (hard GND through the closed contact), pressed = HIGH (line floats, internal
// pull-up defines it). Polarity handled in shutterPressed(); fix on next spin.
constexpr uint8_t PIN_SHUTTER = 1;

static inline bool shutterPressed() {
    return digitalRead(PIN_SHUTTER) == HIGH;
}

// Timing constants
constexpr uint32_t VIEWFINDER_PAUSE_MS = 3000;
constexpr uint32_t IDLE_TIMEOUT_MS = 10000;  // 10 seconds idle -> sleep
constexpr uint32_t HINT_TOAST_DELAY_MS = 5000;  // Show hint after 5s idle

// State variables
static bool lastPressed = false;
static uint32_t viewfinderPauseUntil = 0;
static uint32_t lastActivityTime = 0;
static bool hintToastShowing = false;
static bool wasPreviewActive = false;

// Forward declaration
void enterSleepMode();

// Boot/wake gate, not a debounce: light sleep wakes on a HIGH level, i.e. with
// the button still held, so the loop would otherwise see it as a fresh press.
// Block until the line has been continuously released for stableMs.
static void waitForStableRelease(uint32_t stableMs) {
    uint32_t stableStart = millis();
    while (millis() - stableStart < stableMs) {
        if (shutterPressed()) {
            stableStart = millis();  // Bounce or still held — restart the window
        }
        delay(5);
    }
}

// The only debounce. Pressed is held by the weak internal pull-up (~45k), so a
// single sample can be noise; resampling over ~10ms rejects it. Measured on
// this switch: real presses hold ~200ms, release bounce lasts ~1ms — 10ms sits
// comfortably between the two, so no additional edge-timing window is needed.
static bool confirmPressed() {
    for (int i = 0; i < 5; i++) {
        delay(2);
        if (!shutterPressed()) return false;
    }
    return true;
}

void setup() {
    // Shutter pull-up first — no external pull-up on the carrier, so give the
    // line the whole boot sequence to settle before anyone reads it
    pinMode(PIN_SHUTTER, INPUT_PULLUP);

    Serial.begin(115200);

    // Wait for USB-CDC to enumerate after flash
    delay(1000);
    Serial.println("little-camera init — Xiao_Shutter carrier");

    // Initialize buzzer early for splash melody
    Audio::init(PIN_BUZZ);

    // Initialize Sharp Memory LCD
    Display::init(PIN_LCD_SCLK, PIN_LCD_MOSI, PIN_LCD_CS, PIN_LCD_DISP);
    Serial.println("Display initialized");

    // Show splash screen with ta-da-da melody
    Display::drawSplash();
    Serial.println("Splash screen drawn");
    Audio::playMelody(Audio::Melody::TaDaDa);

    // Play melody during splash (non-blocking update loop)
    uint32_t splashStart = millis();
    while (millis() - splashStart < 1500) {
        Audio::update();
        delay(10);
    }

    // Initialize camera (OV2640 on the Sense B2B connector)
    if (!Camera::init()) {
        Serial.println("Camera init failed — halting");
        while (true) delay(1000);
    }

    // Don't enter the loop until the shutter line is quiet
    waitForStableRelease(50);

    // Initialize activity timer
    lastActivityTime = millis();
}

void enterSleepMode() {
    Serial.println("Entering sleep mode...");

    // Clear any active toast before sleep
    Display::clearToast();
    hintToastShowing = false;

    // Play descending melody before sleep
    Audio::playMelody(Audio::Melody::DaDaTa);
    while (Audio::isPlaying()) {
        Audio::update();
        delay(10);
    }

    // Draw sleep screen
    Display::drawSleep();

    // Configure GPIO wakeup for light sleep (wake on HIGH = button press,
    // inverted polarity — see PIN_SHUTTER note)
    gpio_wakeup_enable((gpio_num_t)PIN_SHUTTER, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Small delay to let display finish and avoid immediate wake
    delay(100);

    // Enter light sleep
    esp_light_sleep_start();

    // Disable GPIO wakeup after waking
    gpio_wakeup_disable((gpio_num_t)PIN_SHUTTER);

    // Woke up! Reset activity timer and hint state
    Serial.println("Woke up from sleep!");
    lastActivityTime = millis();
    hintToastShowing = false;
    Display::clearToast();

    // Play click immediately after wake
    Audio::init(PIN_BUZZ);
    Audio::playClick();

    // Require a continuously-released line before resuming — a plain
    // release-wait plus fixed delay still let release bounce re-trigger
    // a capture right after wake
    waitForStableRelease(100);
    lastPressed = false;
}

void loop() {
    bool pressed = shutterPressed();
    uint32_t now = millis();

    // Check for idle timeout
    if (now - lastActivityTime >= IDLE_TIMEOUT_MS) {
        enterSleepMode();
        return;  // After wake, restart loop fresh
    }

    bool previewActive = now < viewfinderPauseUntil;

    // Reset activity timer when preview ends (so hint timer starts fresh)
    if (wasPreviewActive && !previewActive) {
        lastActivityTime = now;
    }
    wasPreviewActive = previewActive;

    // Update async melody (runs during preview so capture melody plays)
    Audio::update();

    // Show hint toast after 5s of inactivity (but not during preview)
    if (!previewActive && !hintToastShowing && (now - lastActivityTime) >= HINT_TOAST_DELAY_MS) {
        Display::showToast("press to shoot", Display::ToastHAlign::Right, Display::ToastVAlign::Top, false, 0);
        hintToastShowing = true;
    }

    // Only update viewfinder after pause expires
    if (!previewActive) {
        uint8_t* frame = Camera::capture();
        if (frame) {
            Display::drawViewfinder(frame, Camera::WIDTH, Camera::HEIGHT);
        }
    }

    // Toggle VCOM to prevent LCD burn-in
    Display::refresh();

    // Press: capture with nice dither + ta-da-da + pause viewfinder
    // Blocked while preview is active to prevent rapid captures
    if (!previewActive && pressed && !lastPressed && confirmPressed()) {
        Serial.println("Button pressed — capturing");

        // Clear hint toast if showing
        if (hintToastShowing) {
            Display::clearToast();
            hintToastShowing = false;
        }

        // Capture and render with Floyd-Steinberg dithering
        uint8_t* frame = Camera::capture();
        if (frame) {
            Display::drawCapture(frame, Camera::WIDTH, Camera::HEIGHT);
        }

        Audio::playClick();
        Audio::playMelody(Audio::Melody::TaDaDa);
        viewfinderPauseUntil = now + VIEWFINDER_PAUSE_MS;
        lastActivityTime = now;  // Reset idle timer on activity
    }

    lastPressed = pressed;
}
