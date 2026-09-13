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
#include "storage.h"

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
constexpr uint32_t IDLE_TIMEOUT_MS = 10000;  // 10 seconds idle -> sleep
constexpr uint32_t HINT_TOAST_DELAY_MS = 5000;  // Show hint after 5s idle

// How long the bare capture holds before the layout starts sliding, and how
// long the slide itself takes. The dwell exists so the shot registers as its
// own moment instead of being swallowed by the animation.
constexpr uint32_t CAPTURE_DWELL_MS = 350;
constexpr uint32_t SAVE_SLIDE_MS = 280;
// Point in the slide out where the frozen capture hands back to the live
// camera. Grabbing a frame costs a sensor-frame wait, so the live half runs at
// a lower frame rate than the frozen half — starting at the halfway mark keeps
// most of the motion smooth while still masking the dither change.
constexpr float DISMISS_LIVE_AT = 0.5f;

// Save-screen gestures. Long enough that trash reads as a deliberate act rather
// than a slow tap — it's the one irreversible thing the button can do.
constexpr uint32_t TRASH_HOLD_MS = 1500;

// One button drives everything, so the UI is a strict mode machine: the
// meaning of a press depends entirely on which screen is up.
enum class Mode {
    Viewfinder,  // Live preview; press shoots
    Capture,     // Frozen frame: dwell, then slide into Save
    Save,        // Frame plus send/trash; press sends, hold trashes
    Dismissing,  // Slide back out after an action, then resume the viewfinder
};

// State variables
static Mode mode = Mode::Viewfinder;
static bool lastPressed = false;
static uint32_t lastActivityTime = 0;
static bool hintToastShowing = false;
static uint32_t captureShownAt = 0;
// The frame being reviewed. Camera::capture() hands back the driver's
// framebuffer and only recycles it on the next call, so this stays valid as
// long as Capture/Save never grab a new frame — which they don't.
static uint8_t* savedFrame = nullptr;
// Save-screen gesture tracking: when the current press began, whether a press
// is in progress, and whether it already crossed the hold threshold.
static uint32_t pressStartedAt = 0;
static bool gestureActive = false;
static bool holdFired = false;
// Dismiss slide: when it started and the toast to raise once it lands.
static uint32_t dismissStartedAt = 0;
static const char* pendingToast = nullptr;

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

    // Photo storage. Non-fatal: the camera still works without it, saves just
    // report an error, and refusing to boot over a bad partition would be worse.
    Storage::init();

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

    // Camera::deinit() below frees the driver's framebuffer, so any frame we
    // were reviewing dies with it. Drop back to the viewfinder rather than
    // waking into a save screen pointing at freed memory.
    mode = Mode::Viewfinder;
    savedFrame = nullptr;
    pendingToast = nullptr;

    // Play descending melody before sleep
    Audio::playMelody(Audio::Melody::DaDaTa);
    while (Audio::isPlaying()) {
        Audio::update();
        delay(10);
    }

    // Draw sleep screen
    Display::drawSleep();

    // The OV2640 has no PWDN or RESET pin wired on the Sense B2B connector, so
    // light sleep only stops its XCLK — the sensor stays powered and biased at
    // milliamps, dwarfing everything else on the board (~250uA for the sleeping
    // S3, ~50uA for the static panel). Tearing the driver down is the only lever
    // we have; it costs a few hundred ms of re-init on wake.
    Camera::deinit();

    // Configure GPIO wakeup for light sleep (wake on HIGH = button press,
    // inverted polarity — see PIN_SHUTTER note)
    gpio_wakeup_enable((gpio_num_t)PIN_SHUTTER, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Small delay to let display finish and avoid immediate wake
    delay(100);

    // The panel keeps showing the sleep face while we're out, and DISP stays
    // HIGH across light sleep, so VCOM has to keep flipping or the pixels take
    // a permanent DC bias. Nothing runs while esp_light_sleep_start() blocks,
    // so wake ourselves on a timer to do it and go straight back down.
    for (;;) {
        esp_sleep_enable_timer_wakeup((uint64_t)Display::VCOM_INTERVAL_MS * 1000);
        esp_light_sleep_start();

        if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) break;

        // Keep-alive tick: one SPI command, no melody, no re-init.
        Display::toggleVcom();
    }

    // Disable both wake sources after waking for real
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable((gpio_num_t)PIN_SHUTTER);

    // Woke up! Reset activity timer and hint state
    Serial.println("Woke up from sleep!");
    lastActivityTime = millis();
    hintToastShowing = false;
    Display::clearToast();

    // Play click immediately after wake
    Audio::init(PIN_BUZZ);
    Audio::playClick();

    // Bring the sensor back up (torn down before sleep). On failure the loop
    // just gets nullptr frames from capture() and keeps a stale viewfinder,
    // which beats halting a device the user just woke up.
    if (!Camera::init()) {
        Serial.println("Camera re-init failed after wake");
    }

    // Require a continuously-released line before resuming — a plain
    // release-wait plus fixed delay still let release bounce re-trigger
    // a capture right after wake
    waitForStableRelease(100);
    lastPressed = false;
}

// Ease-out cubic. The panel only manages ~10 frames across a slide, and at that
// frame count a linear ramp reads as a mechanical stutter — decelerating into
// the resting layout hides it. Both slides end at rest, so both use it: the
// dismiss just runs the position backwards.
static float easeOutCubic(float p) {
    float inv = 1.0f - p;
    return 1.0f - inv * inv * inv;
}

// Start sliding the save layout back out. The toast waits until the slide
// lands — raising it mid-slide would drop it on top of the columns still
// moving underneath it.
static void startDismiss(const char* toast) {
    pendingToast = toast;
    dismissStartedAt = millis();
    mode = Mode::Dismissing;
    // The slide renders from _photoBits, not from this pointer, and the live
    // half calls Camera::capture() — which recycles the buffer it points at.
    savedFrame = nullptr;
}

// Slide finished: back to live preview, with a toast naming what happened.
static void returnToViewfinder() {
    savedFrame = nullptr;
    mode = Mode::Viewfinder;
    lastActivityTime = millis();
    hintToastShowing = false;
    if (pendingToast) {
        Display::showToast(pendingToast, Display::ToastHAlign::Right,
                           Display::ToastVAlign::Top);
        pendingToast = nullptr;
    }
}

void loop() {
    bool pressed = shutterPressed();
    uint32_t now = millis();

    // Idle timeout. Also applies on the save screen: an unanswered prompt is
    // still an idle device, and the photo is discarded rather than saved —
    // walking away is not consent to keep it.
    if (now - lastActivityTime >= IDLE_TIMEOUT_MS) {
        enterSleepMode();
        return;  // After wake, restart loop fresh
    }

    Audio::update();

    // Toggle VCOM to prevent LCD burn-in. Every mode needs this.
    Display::refresh();

    switch (mode) {
        case Mode::Viewfinder: {
            // Show hint toast after 5s of inactivity
            if (!hintToastShowing && (now - lastActivityTime) >= HINT_TOAST_DELAY_MS) {
                Display::showToast("press to shoot", Display::ToastHAlign::Right,
                                   Display::ToastVAlign::Top, false, 0);
                hintToastShowing = true;
            }

            uint8_t* frame = Camera::capture();
            if (frame) {
                Display::drawViewfinder(frame, Camera::WIDTH, Camera::HEIGHT);
            }

            if (pressed && !lastPressed && confirmPressed()) {
                Serial.println("Shutter — capturing");

                if (hintToastShowing) {
                    Display::clearToast();
                    hintToastShowing = false;
                }

                // Re-grab so the saved frame is the one taken at the press, not
                // the viewfinder frame from the top of this iteration.
                savedFrame = Camera::capture();
                if (savedFrame) {
                    Display::drawCapture(savedFrame, Camera::WIDTH, Camera::HEIGHT);
                }

                Audio::playClick();
                Audio::playMelody(Audio::Melody::TaDaDa);

                captureShownAt = millis();
                lastActivityTime = captureShownAt;
                mode = savedFrame ? Mode::Capture : Mode::Viewfinder;
            }
            break;
        }

        case Mode::Capture: {
            uint32_t elapsed = now - captureShownAt;
            if (elapsed < CAPTURE_DWELL_MS) break;  // Frame frozen, nothing to do

            uint32_t slid = elapsed - CAPTURE_DWELL_MS;
            if (slid < SAVE_SLIDE_MS) {
                Display::drawSaveTransition(easeOutCubic((float)slid / SAVE_SLIDE_MS));
                break;
            }

            Display::drawSave(savedFrame, Camera::WIDTH, Camera::HEIGHT);
            mode = Mode::Save;
            // Start the gesture from a clean slate. If the shutter is still
            // held from the capture, lastPressed is already true and the edge
            // test in Save won't fire until it's released — which is what we
            // want: that press belongs to the capture.
            gestureActive = false;
            holdFired = false;
            break;
        }

        case Mode::Save: {
            if (pressed && !lastPressed && confirmPressed()) {
                pressStartedAt = millis();
                gestureActive = true;
                holdFired = false;
            }
            // Re-read the clock instead of using `now` from the top of loop().
            // confirmPressed() spends 10ms above, so on the iteration that
            // starts a gesture `pressStartedAt` is *later* than `now` and the
            // unsigned subtraction wraps to ~4.29 billion — which cleared the
            // threshold instantly and made every press a trash.
            else if (pressed && gestureActive && !holdFired &&
                     (millis() - pressStartedAt) >= TRASH_HOLD_MS) {
                // Fire on the threshold rather than on release, so the hold has
                // a definite end the user can feel instead of a silent wait.
                Serial.println("Save: trash");
                holdFired = true;
                Audio::playMelody(Audio::Melody::DaDaTa);
                startDismiss("deleted");
            } else if (!pressed && lastPressed && gestureActive && !holdFired) {
                // Released before the threshold: a tap. No modem on this carrier
                // revision, so "send" writes the photo to flash and stops there
                // — the file is staged for whatever eventually uploads it.
                //
                // Done here rather than after the slide so the toast can report
                // what actually happened. The write is ~10KB and stalls the
                // frozen frame briefly before the slide starts, which reads as
                // part of the button press rather than as a dropped frame.
                Storage::Result r = Storage::savePhoto(Display::photoBits(),
                                                       Display::PHOTO_WIDTH,
                                                       Display::PHOTO_HEIGHT);
                Audio::playClick();
                switch (r) {
                    case Storage::Result::Ok:   startDismiss("saved"); break;
                    case Storage::Result::Full: startDismiss("storage full"); break;
                    default:                    startDismiss("save failed"); break;
                }
            }

            if (!pressed) gestureActive = false;
            break;
        }

        case Mode::Dismissing: {
            uint32_t slid = now - dismissStartedAt;
            if (slid >= SAVE_SLIDE_MS) {
                returnToViewfinder();
                break;
            }

            float p = (float)slid / SAVE_SLIDE_MS;
            // Same curve, run backwards: the columns swap places again and
            // settle into the viewfinder layout.
            float t = 1.0f - easeOutCubic(p);

            // Hand the photo back to the live camera partway through rather
            // than at the end. The dither switches Floyd-Steinberg -> Bayer at
            // that instant, and doing it while everything is still sliding
            // hides the change; doing it after the slide lands is a visible
            // pop on an otherwise static screen.
            uint8_t* live = (p >= DISMISS_LIVE_AT) ? Camera::capture() : nullptr;
            Display::drawDismissTransition(live, Camera::WIDTH, Camera::HEIGHT, t);
            break;
        }
    }

    lastPressed = pressed;
}
