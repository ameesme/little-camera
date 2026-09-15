/**
 * little-camera on the Xiao_Shutter carrier (XIAO ESP32-S3 Sense).
 *
 * Viewfinder + capture + sleep app. Cellular is not on this carrier
 * revision, so no modem code. Camera comes from the Sense expansion
 * over the B2B connector.
 */

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include "audio.h"
#include "camera.h"
#include "console.h"
#include "display.h"
#include "identity.h"
#include "storage.h"
#include "sync.h"

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
// Looking at a photo is slower than framing one: the gallery, and the save
// screen with a photo waiting on a decision, get longer before the sleep
// face comes up.
constexpr uint32_t REVIEW_IDLE_TIMEOUT_MS = 30000;
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

// Hold in the viewfinder to open the gallery, hold in the gallery to leave it.
// 3.5x the measured ~200ms press, so a tap never becomes a hold by accident,
// and well short of TRASH_HOLD_MS so the two holds stay apart by feel.
constexpr uint32_t GALLERY_HOLD_MS = 700;

// After the idle timeout the sleep face goes up at once, but the radio keeps
// advertising behind it for this long so a phone can still collect photos.
// Only then does the chip light-sleep (radio off). A phone mid-transfer
// stretches the window.
constexpr uint32_t RADIO_LINGER_MS = 30000;
// Waking — with the radio still on or from light sleep — takes a hold, not a
// tap. A camera in a bag gets tapped; a second of pressure is intent.
constexpr uint32_t WAKE_HOLD_MS = 1000;

// One button drives everything, so the UI is a strict mode machine: the
// meaning of a press depends entirely on which screen is up.
enum class Mode {
    Viewfinder,  // Live preview; press shoots, hold opens the gallery
    Capture,     // Frozen frame: dwell, then slide into Save
    Save,        // Frame plus send/trash; press sends, hold trashes
    Dismissing,  // Slide back out after an action, then resume the viewfinder
    Gallery,     // Stored photos, newest first; press = next, hold = back
    Asleep,      // Sleep face up, radio still advertising; hold wakes
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
// Grab-on-press, commit-on-release: the shutter still fires the instant it is
// pressed (that's what a camera button does), but whether the frozen frame
// goes on to the save screen or gets thrown away for the gallery is decided by
// whether the button is still down at GALLERY_HOLD_MS. True from the press
// edge until that decision is made.
static bool captureHeld = false;
// Gallery: which photo (0 = newest), whether the screen needs redrawing, and
// what the toast looked like at the last redraw — the gallery is static, so it
// only repaints when something about it changed.
static int galleryOrdinal = 0;
static bool galleryDirty = false;
static bool galleryToastWasVisible = false;
// Same for the save screen, which is also drawn once and then left alone.
static bool saveToastWasVisible = false;
// The decoded photo. Its own buffer rather than Display's: 9.6KB is noise next
// to the 77KB camera frame, and Display keeps its bitmaps-in, pixels-out
// contract (which is what lets the preview tool render the gallery).
static uint8_t galleryBits[Display::PHOTO_BYTES * Display::PHOTO_HEIGHT];
// When the sleep face went up (the radio window counts from here).
// Camera::deinit() is not idempotent, so the driver's state is tracked too.
static uint32_t asleepSince = 0;
static bool cameraReady = false;

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
    // Why are we booting? A splash where a sleep face was expected means a
    // reset; this line says which kind (1 power-on, 3 software, 4 panic,
    // 5 interrupt watchdog, 6 task watchdog, 9 brownout, per esp_reset_reason_t).
    Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());

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

    Identity::init();
    Console::begin();

    // Initialize camera (OV2640 on the Sense B2B connector)
    if (!Camera::init()) {
        Serial.println("Camera init failed — halting");
        while (true) delay(1000);
    }
    cameraReady = true;

    // Radio last: everything it advertises (photo counts, the secret) exists
    // by now, and the camera init above is the slow part of boot anyway.
    // -DLC_NO_BLE in platformio.ini builds without the radio, to tell a BLE
    // problem from everything else.
#ifndef LC_NO_BLE
    Sync::begin();
#endif

    // Don't enter the loop until the shutter line is quiet
    waitForStableRelease(50);

    // Initialize activity timer
    lastActivityTime = millis();
}

// True if the shutter stays down for WAKE_HOLD_MS from now; false the moment
// it is released. Polls, so only for the moments when nothing else runs.
static bool heldToWake() {
    uint32_t start = millis();
    while (millis() - start < WAKE_HOLD_MS) {
        if (!shutterPressed()) return false;
        delay(10);
    }
    return true;
}

// Idle timeout: sleep face up, camera driver down, radio still on. To the
// user this is sleep; to a phone it is another RADIO_LINGER_MS of chances to
// collect photos before the chip really goes down (enterSleepMode). A photo
// under review is discarded: walking away is not consent to keep it.
static void enterAsleep() {
    Serial.println("Asleep (radio on)");
    Display::clearToast();
    hintToastShowing = false;
    pendingToast = nullptr;
    savedFrame = nullptr;
    captureHeld = false;
    Audio::playMelody(Audio::Melody::DaDaTa);
    Display::drawSleep();
    // The sensor's work is done: tear the driver down as sleep does
    // (docs/camera_standby.md covers what that does and doesn't save).
    if (cameraReady) {
        Camera::deinit();
        cameraReady = false;
    }
    asleepSince = millis();
    gestureActive = false;
    holdFired = false;
    mode = Mode::Asleep;
}

// A one-second hold on the sleeping camera: back to the viewfinder, at the
// moment the hold completes rather than on release. The shutter is still
// down on the way out and the viewfinder acts on press edges only, so the
// hold does not shoot.
static void wakeFromAsleep() {
    Serial.println("Woke (radio was on)");
    Audio::init(PIN_BUZZ);
    Audio::playClick();  // Bit-banged, so it is heard even straight out of sleep
    if (!cameraReady) {
        cameraReady = Camera::init();
        if (!cameraReady) Serial.println("Camera re-init failed after wake");
    }
    mode = Mode::Viewfinder;
    hintToastShowing = false;
    gestureActive = false;
    holdFired = false;
    lastActivityTime = millis();
}

// Sync raises its events from loop() on the main thread, so drawing here is
// safe. Toasts are the whole UI the radio gets: connect, the pairing code,
// and a running count of what left the device.
static void handleSyncEvents() {
    Sync::Event ev;
    while (Sync::nextEvent(&ev)) {
        char text[32];
        // Asleep means the sleep face stays up. The one exception is a
        // pairing code, which only appears because someone is actively
        // pairing and needs to read it: that wakes the camera.
        if (mode == Mode::Asleep) {
            if (ev.kind != Sync::Event::Passkey) continue;
            wakeFromAsleep();
        }
        switch (ev.kind) {
            case Sync::Event::Connected:
                Display::showToast("phone connected", Display::ToastHAlign::Right,
                                   Display::ToastVAlign::Top);
                break;
            case Sync::Event::Disconnected:
                break;
            case Sync::Event::Passkey:
                // Indefinite: it stays until pairing finishes one way or the other.
                snprintf(text, sizeof(text), "pair %06lu", (unsigned long)ev.value);
                Display::showToast(text, Display::ToastHAlign::Right, Display::ToastVAlign::Top,
                                   true, 0);
                break;
            case Sync::Event::PairingDone:
                Display::showToast(ev.value ? "paired" : "pairing failed",
                                   Display::ToastHAlign::Right, Display::ToastVAlign::Top);
                break;
            case Sync::Event::Sent:
                snprintf(text, sizeof(text), "sent %lu", (unsigned long)ev.value);
                Display::showToast(text, Display::ToastHAlign::Right, Display::ToastVAlign::Top);
                break;
            default:
                continue;
        }
        // The gallery only repaints on change; a new toast text is a change.
        galleryDirty = true;
    }
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
    captureHeld = false;

    // Sleep face and melody are normally already done by enterAsleep(); this
    // keeps the direct path (and the preview's expectations) intact.
    while (Audio::isPlaying()) {
        Audio::update();
        delay(10);
    }
    Display::drawSleep();

    // Radio off before the camera: light sleep and a live BLE controller is
    // undefined territory in this Arduino core, and the phone is told nothing
    // — it reconnects when the camera advertises again after wake.
#ifndef LC_NO_BLE
    Sync::end();
#endif

    // The OV2640 has no PWDN or RESET pin wired on the Sense B2B connector, so
    // light sleep only stops its XCLK — the sensor stays powered and biased at
    // milliamps, dwarfing everything else on the board (~250uA for the sleeping
    // S3, ~50uA for the static panel). Tearing the driver down is the only lever
    // we have; it costs a few hundred ms of re-init on wake. (Usually already
    // done by enterAsleep(), which precedes this.)
    if (cameraReady) {
        Camera::deinit();
        cameraReady = false;
    }

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
    //
    // Only the shutter (GPIO) ends the sleep. Anything else — the BT
    // controller's own wake source if the radio was not fully down, a
    // rejected sleep call, an undefined cause — is not a person, and treating
    // it as one is exactly how the camera "wakes by itself".
    for (;;) {
        esp_sleep_enable_timer_wakeup((uint64_t)Display::VCOM_INTERVAL_MS * 1000);
        esp_err_t err = esp_light_sleep_start();
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

        if (err != ESP_OK) {
            // Refused to sleep at all (a wake source already pending). The
            // cause is stale in that case, so don't read anything into it.
            Serial.printf("Sleep: rejected (%d), retrying\n", (int)err);
            delay(50);
            continue;
        }
        if (cause == ESP_SLEEP_WAKEUP_GPIO) {
            // The button — but a tap is not enough. Hold to wake; a release
            // before WAKE_HOLD_MS goes straight back to sleep.
            if (heldToWake()) break;
            Serial.println("Sleep: tap ignored, hold to wake");
            continue;
        }
        if (cause == ESP_SLEEP_WAKEUP_TIMER) {
            // Keep-alive tick: one SPI command, no melody, no re-init.
            Display::toggleVcom();
            continue;
        }
        Serial.printf("Sleep: ignoring wake cause %d\n", (int)cause);
        delay(20);  // Don't spin hot if the source keeps firing
    }

    // Disable both wake sources after waking for real
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable((gpio_num_t)PIN_SHUTTER);

    // Woke up! Reset activity timer and hint state
    Serial.println("Woke up from sleep!");
    lastActivityTime = millis();
    hintToastShowing = false;
    Display::clearToast();

    // The wake-up click, on the hold itself. Bit-banged (see Audio::playClick),
    // which is what makes it audible straight out of light sleep.
    Audio::init(PIN_BUZZ);
    Audio::playClick();

    // Bring the sensor back up (torn down before sleep). On failure the loop
    // just gets nullptr frames from capture() and keeps a stale viewfinder,
    // which beats halting a device the user just woke up.
    cameraReady = Camera::init();
    if (!cameraReady) {
        Serial.println("Camera re-init failed after wake");
    }

#ifndef LC_NO_BLE
    Sync::begin();
#endif

    // The shutter is still held (that is what woke us), and the viewfinder
    // only acts on a press *edge*: marking it as already pressed means the
    // hold that woke the camera doesn't also shoot, without making the user
    // let go first. Release bounce is covered by confirmPressed() as usual.
    lastPressed = true;
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
    Display::clearToast();
    pendingToast = toast;
    dismissStartedAt = millis();
    mode = Mode::Dismissing;
    // The slide renders from _photoBits, not from this pointer, and the live
    // half calls Camera::capture() — which recycles the buffer it points at.
    savedFrame = nullptr;
}

// Draw the current gallery photo. Files that fail to decode are skipped — a
// partial write can't leave one behind (savePhoto removes them), but a file
// copied onto the flash by some future tool might be the wrong size.
static void showGalleryPhoto() {
    galleryDirty = false;
    int total = Storage::photoCount();
    if (total <= 0) {
        Display::drawGalleryEmpty();
        return;
    }
    if (galleryOrdinal >= total) galleryOrdinal = 0;
    for (int tries = 0; tries < total; tries++) {
        int index = Storage::photoAt(galleryOrdinal);
        if (index > 0 && Storage::loadPhoto(index, galleryBits, sizeof(galleryBits))) {
            Display::drawGallery(galleryBits, galleryOrdinal, total);
            return;
        }
        Serial.printf("Gallery: could not load #%04d, skipping\n", index);
        galleryOrdinal = (galleryOrdinal + 1) % total;
    }
    Display::drawGalleryEmpty();
}

// Open the gallery. Called while the shutter is still held, so lastPressed is
// already true and the gallery's press-edge test can't fire until the button
// is released — the hold that opened it doesn't also advance it.
static void enterGallery() {
    // The grabbed frame is the driver's framebuffer; dropping it costs nothing.
    savedFrame = nullptr;
    captureHeld = false;
    galleryOrdinal = 0;
    gestureActive = false;
    holdFired = false;
    Display::clearToast();
    hintToastShowing = false;
    galleryToastWasVisible = false;
    mode = Mode::Gallery;
    lastActivityTime = millis();
    Audio::playClick(true);  // Light: into the gallery
    Serial.println("Gallery: open");
    showGalleryPhoto();
}

// Back to the viewfinder. Also called mid-hold, and the viewfinder's own
// press-edge test likewise waits for a release, so leaving never shoots.
static void leaveGallery() {
    mode = Mode::Viewfinder;
    lastActivityTime = millis();
    hintToastShowing = false;
    Display::clearToast();
    Audio::playClick();  // Regular: back to the camera
    Serial.println("Gallery: close");
}

// Slide finished: back to live preview, with a toast naming what happened.
static void returnToViewfinder() {
    savedFrame = nullptr;
    mode = Mode::Viewfinder;
    lastActivityTime = millis();
    hintToastShowing = false;
    Display::clearToast();
    if (pendingToast) {
        Display::showToast(pendingToast, Display::ToastHAlign::Right,
                           Display::ToastVAlign::Top);
        pendingToast = nullptr;
    }
}

void loop() {
    bool pressed = shutterPressed();
    uint32_t now = millis();

    // Idle timeout: sleep face up and radio still on first, light sleep later
    // from the Asleep case below. Also applies on the save screen: an
    // unanswered prompt is still an idle device.
    const uint32_t idleLimit =
        (mode == Mode::Gallery || mode == Mode::Save) ? REVIEW_IDLE_TIMEOUT_MS : IDLE_TIMEOUT_MS;
    if (mode != Mode::Asleep && now - lastActivityTime >= idleLimit) {
        enterAsleep();
    }

    Audio::update();
    Sync::loop();
    handleSyncEvents();

    // USB console: an export in progress is activity. It holds off sleep,
    // and keeps a sleeping camera's radio on, without waking the screen.
    if (Console::poll()) {
        lastActivityTime = millis();
        asleepSince = millis();
    }

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

                // A screen switch always drops the toast. A timed toast only
                // expires when something re-renders, and the capture screen
                // is drawn once — carried over, it would sit there for good.
                Display::clearToast();
                hintToastShowing = false;

                // Re-grab so the saved frame is the one taken at the press, not
                // the viewfinder frame from the top of this iteration.
                savedFrame = Camera::capture();
                if (savedFrame) {
                    Display::drawCapture(savedFrame, Camera::WIDTH, Camera::HEIGHT);
                }

                Audio::playClick();

                // Fresh clock, same rule as the Save hold: confirmPressed()
                // just spent 10ms, so `now` is stale.
                pressStartedAt = millis();
                captureShownAt = pressStartedAt;
                captureHeld = savedFrame != nullptr;
                lastActivityTime = captureShownAt;
                mode = savedFrame ? Mode::Capture : Mode::Viewfinder;
            }
            break;
        }

        case Mode::Capture: {
            if (captureHeld) {
                if (pressed) {
                    // Still deciding. The frozen frame stays up — for a normal
                    // tap this window closes inside the dwell, so nothing
                    // looks different from a plain capture.
                    if (millis() - pressStartedAt >= GALLERY_HOLD_MS) enterGallery();
                    break;
                }
                // Released before the threshold: it's a shot.
                captureHeld = false;
                Audio::playMelody(Audio::Melody::TaDaDa);
                // Held past the dwell? Start the slide now rather than jumping
                // into the middle of it.
                if (millis() - captureShownAt > CAPTURE_DWELL_MS) {
                    captureShownAt = millis() - CAPTURE_DWELL_MS;
                }
            }

            uint32_t elapsed = millis() - captureShownAt;
            if (elapsed < CAPTURE_DWELL_MS) break;  // Frame frozen, nothing to do

            uint32_t slid = elapsed - CAPTURE_DWELL_MS;
            if (slid < SAVE_SLIDE_MS) {
                Display::drawSaveTransition(easeOutCubic((float)slid / SAVE_SLIDE_MS));
                break;
            }

            Display::clearToast();
            saveToastWasVisible = false;
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
                    // Full means every photo on the flash is one the phone
                    // hasn't pulled yet; syncing frees the room.
                    case Storage::Result::Full: startDismiss("full, sync first"); break;
                    default:                    startDismiss("save failed"); break;
                }
            }

            if (!pressed) gestureActive = false;

            // Static screen: repaint only when a toast comes or goes, so a
            // "phone connected" raised here also goes away again.
            if (mode == Mode::Save) {
                bool toastNow = Display::toastVisible();
                if (toastNow != saveToastWasVisible) {
                    saveToastWasVisible = toastNow;
                    Display::drawSave(savedFrame, Camera::WIDTH, Camera::HEIGHT);
                }
            }
            break;
        }

        case Mode::Gallery: {
            if (pressed && !lastPressed && confirmPressed()) {
                pressStartedAt = millis();
                gestureActive = true;
                holdFired = false;
                lastActivityTime = pressStartedAt;  // Browsing is activity
            } else if (pressed && gestureActive && !holdFired &&
                       (millis() - pressStartedAt) >= GALLERY_HOLD_MS) {
                // Fires on the threshold, like trash, so the hold has an end.
                holdFired = true;
                leaveGallery();
                break;
            } else if (!pressed && lastPressed && gestureActive && !holdFired) {
                // A tap: next photo, wrapping. With one forward gesture,
                // stopping at the oldest would strand the user; the counter
                // makes the wrap legible.
                int total = Storage::photoCount();
                if (total > 0) {
                    galleryOrdinal = (galleryOrdinal + 1) % total;
                    galleryDirty = true;
                }
                Audio::playClick(true);
            }
            if (!pressed) gestureActive = false;

            // Static screen: repaint only when the photo changed or a toast
            // came or went (the sync service raises toasts from outside).
            bool toastNow = Display::toastVisible();
            if (galleryDirty || toastNow != galleryToastWasVisible) {
                galleryToastWasVisible = toastNow;
                showGalleryPhoto();
            }
            break;
        }

        case Mode::Asleep: {
            if (pressed && !lastPressed && confirmPressed()) {
                pressStartedAt = millis();
                gestureActive = true;
            } else if (pressed && gestureActive && (millis() - pressStartedAt) >= WAKE_HOLD_MS) {
                wakeFromAsleep();
                break;
            }
            if (!pressed) gestureActive = false;

            // Radio window over? A phone that is busy with us extends it; so
            // does console traffic (above). Fresh millis(): asleepSince may
            // have been set later in this very iteration than `now`.
            if (millis() - asleepSince >= RADIO_LINGER_MS && !Sync::activeRecently(millis())) {
                enterSleepMode();
                return;  // After wake, restart loop fresh
            }
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
