/**
 * Basic GPIO initialization for LilyGO S3_CAM_SIM v1.2
 *
 * Pulls all project GPIO lines to known-safe states.
 * Does NOT initialize SD/SDMMC — those pins are repurposed for display/buzzer.
 */

#include <Arduino.h>
#include "audio.h"
#include "camera.h"
#include "display.h"

// Display (Sharp Memory LCD) — directly driven, no SD conflict
constexpr uint8_t PIN_DISP_SCLK = 39;
constexpr uint8_t PIN_DISP_MOSI = 38;
constexpr uint8_t PIN_DISP_CS   = 40;

// Buzzer
constexpr uint8_t PIN_BUZZER = 47;

// Button (active-low with external pull-up assumed)
constexpr uint8_t PIN_BUTTON = 21;

// Modem (SIM7080G)
constexpr uint8_t PIN_MODEM_TX  = 45;
constexpr uint8_t PIN_MODEM_RX  = 46;
constexpr uint8_t PIN_MODEM_PWR = 48;

void setup() {
    Serial.begin(115200);

    // Wait for USB-CDC to enumerate after flash
    delay(1000);
    Serial.println("little-camera init");

    // Initialize Sharp Memory LCD
    Display::init(PIN_DISP_SCLK, PIN_DISP_MOSI, PIN_DISP_CS);
    Serial.println("Display initialized");

    // Show splash screen for 1 second
    Display::drawSplash();
    Serial.println("Splash screen drawn");
    delay(1000);

    // Initialize camera
    if (!Camera::init()) {
        Serial.println("Camera init failed — halting");
        while (true) delay(1000);
    }

    // Buzzer
    Audio::init(PIN_BUZZER);

    // Button — input with internal pull-up, wire to GND
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // Modem power/reset — drive low (modem off)
    pinMode(PIN_MODEM_PWR, OUTPUT);
    digitalWrite(PIN_MODEM_PWR, LOW);

    // Modem UART lines — TX as output low, RX as input
    pinMode(PIN_MODEM_TX, OUTPUT);
    pinMode(PIN_MODEM_RX, INPUT);
    digitalWrite(PIN_MODEM_TX, LOW);

    Serial.println("GPIO initialized — all outputs low");
}

static bool lastButtonState = HIGH;  // NO button: default open = HIGH
static uint32_t lastPressTime = 0;
static uint32_t lastReleaseTime = 0;
static uint32_t viewfinderPauseUntil = 0;  // Pause viewfinder until this time
constexpr uint32_t DEBOUNCE_MS = 100;
constexpr uint32_t VIEWFINDER_PAUSE_MS = 3000;

void loop() {
    bool buttonState = digitalRead(PIN_BUTTON);
    uint32_t now = millis();

    bool previewActive = now < viewfinderPauseUntil;

    // Update async melody (runs during preview so capture melody plays)
    Audio::update();

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
    if (!previewActive && buttonState == LOW && lastButtonState == HIGH && (now - lastPressTime) > DEBOUNCE_MS) {
        Serial.println("Button pressed — capturing");

        // Capture and render with Floyd-Steinberg dithering
        uint8_t* frame = Camera::capture();
        if (frame) {
            Display::drawCapture(frame, Camera::WIDTH, Camera::HEIGHT);
        }

        Audio::playClick();
        Audio::playMelody(Audio::Melody::TaDaDa);
        viewfinderPauseUntil = now + VIEWFINDER_PAUSE_MS;
        lastPressTime = now;
    }

    // Release: just update state
    if (buttonState == HIGH && lastButtonState == LOW && (now - lastReleaseTime) > DEBOUNCE_MS) {
        lastReleaseTime = now;
    }

    lastButtonState = buttonState;
}
