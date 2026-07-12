/**
 * Basic GPIO initialization for LilyGO S3_CAM_SIM v1.2
 *
 * Pulls all project GPIO lines to known-safe states.
 * Does NOT initialize SD/SDMMC — those pins are repurposed for display/buzzer.
 */

#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
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

// Timing constants
constexpr uint32_t DEBOUNCE_MS = 100;
constexpr uint32_t VIEWFINDER_PAUSE_MS = 3000;
constexpr uint32_t IDLE_TIMEOUT_MS = 10000;  // 10 seconds idle -> sleep

// State variables
static bool lastButtonState = HIGH;
static uint32_t lastPressTime = 0;
static uint32_t lastReleaseTime = 0;
static uint32_t viewfinderPauseUntil = 0;
static uint32_t lastActivityTime = 0;

// Forward declaration
void enterSleepMode();

void setup() {
    Serial.begin(115200);

    // Wait for USB-CDC to enumerate after flash
    delay(1000);
    Serial.println("little-camera init");

    // Initialize buzzer early for splash melody
    Audio::init(PIN_BUZZER);

    // Initialize Sharp Memory LCD
    Display::init(PIN_DISP_SCLK, PIN_DISP_MOSI, PIN_DISP_CS);
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

    // Initialize camera
    if (!Camera::init()) {
        Serial.println("Camera init failed — halting");
        while (true) delay(1000);
    }

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

    // Initialize activity timer
    lastActivityTime = millis();
}

void enterSleepMode() {
    Serial.println("Entering sleep mode...");

    // Play descending melody before sleep
    Audio::playMelody(Audio::Melody::DaDaTa);
    while (Audio::isPlaying()) {
        Audio::update();
        delay(10);
    }

    // Draw sleep screen
    Display::drawSleep();

    // Configure GPIO wakeup for light sleep (wake on LOW = button press)
    gpio_wakeup_enable((gpio_num_t)PIN_BUTTON, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Small delay to let display finish and avoid immediate wake
    delay(100);

    // Enter light sleep
    esp_light_sleep_start();

    // Disable GPIO wakeup after waking
    gpio_wakeup_disable((gpio_num_t)PIN_BUTTON);

    // Woke up! Reset activity timer
    Serial.println("Woke up from sleep!");
    lastActivityTime = millis();

    // Play click immediately after wake
    Audio::init(PIN_BUZZER);
    Audio::playClick();

    // Wait for button release to avoid immediate re-trigger
    while (digitalRead(PIN_BUTTON) == LOW) {
        delay(10);
    }
    delay(50);  // Debounce
}

void loop() {
    bool buttonState = digitalRead(PIN_BUTTON);
    uint32_t now = millis();

    // Check for idle timeout
    if (now - lastActivityTime >= IDLE_TIMEOUT_MS) {
        enterSleepMode();
        return;  // After wake, restart loop fresh
    }

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
        lastActivityTime = now;  // Reset idle timer on activity
    }

    // Release: just update state
    if (buttonState == HIGH && lastButtonState == LOW && (now - lastReleaseTime) > DEBOUNCE_MS) {
        lastReleaseTime = now;
    }

    lastButtonState = buttonState;
}
