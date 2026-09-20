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
#include <esp_random.h>
#include <esp_timer.h>
#include <time.h>
#include "audio.h"
#include "chirp.h"
#include "camera.h"
#include "console.h"
#include "display.h"
#include "identity.h"
#include "mood.h"
#include "mood_store.h"
#include "ota.h"
#include "storage.h"
#include "sync.h"
#include "sync_protocol.h"
#include "version.h"

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
// Pairing holds the screen until the phone is done with it. The timeout is the
// escape hatch for a phone that walks away mid-dialog; the result lingers just
// long enough to be read.
constexpr uint32_t PAIRING_TIMEOUT_MS = 60000;
constexpr uint32_t PAIRING_RESULT_MS = 1500;

// A firmware update owns the screen until the phone is done with it or gives
// up; sync.cpp's own timeouts are the escape hatch, so there is none here.
// How long the outcome stays up afterwards, and how long a freshly updated
// image has to prove it can stay on its feet before the trial ends
// (docs/protocol.md §3.7). Twenty seconds covers boot, the camera, the radio
// and the first idle timeout — an image that cannot manage that is one a
// rollback can actually fix.
constexpr uint32_t UPDATE_RESULT_MS = 3000;
constexpr uint32_t OTA_CONFIRM_MS = 20000;

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
    Pairing,     // The phone is asking for the passkey; the code owns the screen
    Updating,    // New firmware coming in over BLE; progress owns the screen
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
// Pairing: where to go back to, when it started, and when it ended (0 = still
// waiting on the phone).
static Mode modeBeforePairing = Mode::Viewfinder;
static uint32_t pairingStartedAt = 0;
static uint32_t pairingDoneAt = 0;
// Updating: where to go back to if it does not end in a reboot, the percent
// on screen, and when the outcome went up (0 = still running).
static Mode modeBeforeUpdate = Mode::Viewfinder;
static int updatePercent = -1;
static uint32_t updateEndedAt = 0;

// Forward declaration
void enterSleepMode();

// ---- Mood (docs/mood.md) ---------------------------------------------------
//
// The state machine lives in mood.h and knows nothing about hardware; this is
// its clock, its flash and its buzzer. Uptime comes from esp_timer: 64-bit,
// runs through light sleep, never wraps. The wall clock only exists once a
// phone has set it. -DLC_MOOD_FAST makes uptime run 600x — an hour in six
// seconds, the whole week in about seventeen minutes — for bench testing.
constexpr uint32_t BREATH_MS = 5000;  // Sleep face breath period, awake; asleep it is the VCOM tick

static Mood::State moodState;
static Chirp::Rng moodRng(1);
static int16_t moodTzMin = 0;
static bool moodTzKnown = false;
static uint8_t sleepFaceBand = 0xFF;   // Band the face was last drawn for
static bool sleepBreathIn = false;
static uint32_t sleepFaceDrawnAt = 0;

static Mood::Clock moodClock() {
    Mood::Clock c;
    uint64_t up = (uint64_t)esp_timer_get_time() / 1000000ULL;
#ifdef LC_MOOD_FAST
    up *= 600;
#endif
    c.uptimeS = (uint32_t)up;
    time_t t = time(nullptr);
    c.epoch = (t > 0 && (uint64_t)t >= Mood::EPOCH_VALID_FROM) ? (uint32_t)t : 0;
    c.tzMin = moodTzMin;
    c.tzKnown = moodTzKnown;
    return c;
}

static void moodPersist(const char* why) {
    Mood::Clock c = moodClock();
    MoodStore::save(Mood::snapshot(moodState, c));
    Mood::markPersisted(moodState, c);
    Serial.printf("Mood: saved (%s) elapsed=%lus happiness=%u\n", why,
                  (unsigned long)Mood::elapsed(moodState, c), Mood::happiness(moodState, c));
}

// After Storage::init(): the newest photo's header is the fallback when NVS
// has no mood yet (first boot of this firmware on a camera with photos).
static void moodBoot() {
    moodRng = Chirp::Rng(esp_random());
    moodTzKnown = MoodStore::loadTz(&moodTzMin);
    Mood::Persisted p;
    const bool have = MoodStore::load(&p);
    uint32_t newestEpoch = 0;
    Storage::PhotoInfo info;
    const int newest = Storage::newestIndex();
    if (newest > 0 && Storage::photoInfo(newest, &info)) newestEpoch = info.epoch;
    Mood::Clock c = moodClock();
    Mood::boot(moodState, c, have ? &p : nullptr, newestEpoch, moodRng);
    Serial.printf("Mood: %s elapsed=%lus happiness=%u band=%u clock=%s tz=%s%d\n", have ? "resumed" : "fresh",
                  (unsigned long)Mood::elapsed(moodState, c), Mood::happiness(moodState, c),
                  Chirp::band(Mood::happiness(moodState, c)), c.epoch ? "set" : "unset",
                  moodTzKnown ? "" : "unknown ", moodTzMin);
    if (!have) moodPersist("first boot");
}

static uint8_t moodHappiness() { return Mood::happiness(moodState, moodClock()); }

static void moodOnPhoto() {
    Mood::onPhoto(moodState, moodClock(), moodRng);
    moodPersist("photo");
}

// The phone set the clock (and maybe told us the timezone).
static void moodOnClock(bool tzKnown, int16_t tzMin) {
    if (tzKnown && (!moodTzKnown || moodTzMin != tzMin)) {
        moodTzMin = tzMin;
        moodTzKnown = true;
        MoodStore::saveTz(tzMin);
    }
    Mood::onClockSet(moodState, moodClock());
    moodPersist("clock");
}

// Compose, log, then play. The play blocks for up to 300ms, so the line is on
// the monitor before the sound. Audio::init first: this is also called straight
// out of light sleep, where the pin has to be claimed again.
static void playMoodChirp(uint8_t happiness, const char* why) {
    Chirp::Rng rng(esp_random());
    Chirp::Score s = Chirp::compose(happiness, rng);
    char desc[64];
    Chirp::describe(s, desc, sizeof(desc));
    Serial.printf("%s: chirp happiness=%u/255 band=%u %s = %lums\n", why, happiness,
                  Chirp::band(happiness), desc, (unsigned long)Chirp::totalMs(s));
    Audio::init(PIN_BUZZ);
    Audio::playChirp(s);
}

// Ask the mood what it owes and pay it if this is a good moment. The state
// machine never knows what screen is up; `quiet` is decided by the caller:
// a screen a chirp may interrupt, button up, nothing else sounding, no
// transfer running. A chirp that is not paid stays due for the next call.
static void serviceMood(bool quiet, const char* why) {
    Mood::Clock c = moodClock();
    Mood::Due d = Mood::poll(moodState, c, moodRng);
    if (d != Mood::Due::None && quiet) {
        playMoodChirp(Mood::happiness(moodState, c), why);
        Mood::onChirpPlayed(moodState, c, d, moodRng);
        moodPersist(d == Mood::Due::HourChirp ? "hour chirp" : "attention chirp");
    } else if (Mood::persistDue(moodState, c)) {
        moodPersist("cadence");
    }
}

// The sleep face for the current mood. A full frame when it first goes up or
// the band moved; a breath (the other frame, face rows only) otherwise.
static void showSleepFace(bool full) {
    const uint8_t h = Mood::happiness(moodState, moodClock());
    const uint8_t band = Chirp::band(h);
    if (band != sleepFaceBand) full = true;
    sleepFaceBand = band;
    Display::drawSleep(h, sleepBreathIn, !full);
    sleepFaceDrawnAt = millis();
}

static void breatheSleepFace() {
    sleepBreathIn = !sleepBreathIn;
    showSleepFace(false);
}

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

    // Before anything else is brought up: is this the first boot of an image
    // the phone sent, and has it had too many goes at it? A rollback reboots
    // from here, so it costs nothing but the reset it already spent.
    Ota::begin();

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
    moodBoot();
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
    sleepBreathIn = false;
    showSleepFace(true);
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

// The passkey takes the whole screen. As a toast it was both too small to read
// out and too easy to lose: the viewfinder's own "press to shoot" hint painted
// straight over it. The camera does not wake for this — pairing needs the
// radio and the panel, not the sensor — so a sleeping camera pairs and goes
// back to its face.
static void enterPairing(uint32_t code) {
    char text[16];
    snprintf(text, sizeof(text), "%06lu", (unsigned long)code);
    if (mode != Mode::Pairing) modeBeforePairing = mode;
    Display::clearToast();
    hintToastShowing = false;
    savedFrame = nullptr;
    captureHeld = false;
    gestureActive = false;
    holdFired = false;
    mode = Mode::Pairing;
    pairingStartedAt = millis();
    pairingDoneAt = 0;
    Display::drawPairing(text, "type this on your phone");
    Serial.printf("Pairing: code %s\n", text);
}

// Back to whatever was up before. A photo under review is not worth restoring
// through a pairing dialog, so anything that was not the sleep face resumes as
// the viewfinder.
static void leavePairing() {
    if (modeBeforePairing == Mode::Asleep) {
        mode = Mode::Asleep;
        // The phone that just paired is about to sync: give the radio its full
        // window again rather than whatever was left of the old one.
        asleepSince = millis();
        sleepBreathIn = false;
        showSleepFace(true);
    } else {
        mode = Mode::Viewfinder;
        lastActivityTime = millis();
    }
    hintToastShowing = false;
    gestureActive = false;
    holdFired = false;
    Serial.println("Pairing: over");
}

// New firmware is coming in. It takes minutes and cannot be interrupted, so it
// takes the screen the way pairing does — and the idle timeout is off while it
// runs (see loop()), or the camera would fall asleep halfway through and take
// the radio down with it.
static void enterUpdating(uint32_t size) {
    if (mode != Mode::Updating) modeBeforeUpdate = mode;
    Display::clearToast();
    hintToastShowing = false;
    pendingToast = nullptr;
    savedFrame = nullptr;
    captureHeld = false;
    gestureActive = false;
    holdFired = false;
    // Nothing is going to be photographed for the next few minutes, and the
    // sensor is milliamps and 77KB of DRAM that the flash writer would rather
    // have. Same teardown as the idle timeout does.
    if (cameraReady) {
        Camera::deinit();
        cameraReady = false;
    }
    mode = Mode::Updating;
    updateEndedAt = 0;
    updatePercent = 0;
    Display::drawUpdate("new firmware", 0, "keep the phone close");
    Serial.printf("Update: receiving %u bytes, fw now %s\n", (unsigned)size, LC_VERSION_STRING);
}

// Back to whatever was up before, after an update that did not end in a
// reboot. A photo under review is long gone; anything that was not the sleep
// face resumes as the viewfinder, exactly as pairing does.
static void leaveUpdating() {
    if (modeBeforeUpdate == Mode::Asleep) {
        mode = Mode::Asleep;
        // The phone may well try again: give the radio a full window.
        asleepSince = millis();
        sleepBreathIn = false;
        showSleepFace(true);
    } else {
        if (!cameraReady) {
            cameraReady = Camera::init();
            if (!cameraReady) Serial.println("Camera re-init failed after the update");
        }
        mode = Mode::Viewfinder;
        lastActivityTime = millis();
    }
    hintToastShowing = false;
    gestureActive = false;
    holdFired = false;
}

// The image is on flash, verified and armed. Everything after this line is
// about leaving cleanly: the phone should see the link close rather than
// vanish, and the melody is the last thing this firmware does.
static void finishUpdating() {
    Serial.println("Update: verified and armed — restarting into it");
    Display::drawUpdate("restarting", -1, nullptr);
    Audio::playMelody(Audio::Melody::TaDaDa);
    while (Audio::isPlaying()) {
        Audio::update();
        delay(10);
    }
#ifndef LC_NO_BLE
    Sync::end();
#endif
    delay(200);
    esp_restart();
}

// It did not work. Say so, and say the part that matters: the camera is still
// the camera it was a minute ago.
static void failUpdating(uint32_t status) {
    if (mode != Mode::Updating) return;
    const bool stopped = status == SyncProto::STATUS_ABORTED;
    Serial.printf("Update: %s (status %u)\n", stopped ? "stopped" : "failed", (unsigned)status);
    Display::drawUpdate(stopped ? "update stopped" : "update failed", -1, "nothing changed");
    Audio::playMelody(Audio::Melody::DaDaTa);
    updateEndedAt = millis();
}

// Sync raises its events from loop() on the main thread, so drawing here is
// safe. Only pairing has a screen: connecting and transferring are the phone's
// business, and it shows its own progress.
static void handleSyncEvents() {
    Sync::Event ev;
    while (Sync::nextEvent(&ev)) {
        switch (ev.kind) {
            case Sync::Event::ClockSet:
                // Never a screen change: the mood just learns the time.
                moodOnClock((ev.value & Sync::CLOCK_TZ_KNOWN) != 0, (int16_t)(uint16_t)(ev.value & 0xFFFF));
                break;
            case Sync::Event::Passkey:
                enterPairing(ev.value);
                break;
            case Sync::Event::PairingDone:
                if (mode == Mode::Pairing) {
                    pairingDoneAt = millis();
                    Display::drawPairing(ev.value ? "paired" : "pairing failed", nullptr);
                }
                break;
            case Sync::Event::UpdateBegan:
                enterUpdating(ev.value);
                break;
            case Sync::Event::UpdateProgress:
                // Bar only: a whole frame is 50ms of SPI, long enough for the
                // radio to outrun the chunk ring behind it.
                if (mode == Mode::Updating && !updateEndedAt && (int)ev.value != updatePercent) {
                    updatePercent = (int)ev.value;
                    Display::drawUpdateProgress(updatePercent);
                }
                break;
            case Sync::Event::UpdateReady:
                finishUpdating();  // Does not return
                break;
            case Sync::Event::UpdateFailed:
                failUpdating(ev.value);
                break;
            default:
                break;  // Connected, Disconnected, Sent: no screen of their own
        }
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
    showSleepFace(true);

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
            // A tap does not wake, but it is answered: a chirp at a random
            // happiness, so a press in a bag is heard for what it was.
            Serial.println("Sleep: tap, hold to wake");
            playMoodChirp(moodHappiness(), "Sleep tap");
            continue;
        }
        if (cause == ESP_SLEEP_WAKEUP_TIMER) {
            // Keep-alive tick, and the mood's clock while asleep: flip VCOM,
            // breathe (face rows only), pay a due chirp. Radio and camera
            // are down here, so a 300ms chirp costs nothing but the sound.
            Display::toggleVcom();
            breatheSleepFace();
            serviceMood(true, "Sleep");
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
    // Same slide as capture -> save, with the newest photo standing in for the
    // viewfinder from the first frame. Blocking is fine here: the button is
    // still held (the hold is what got us here) and the gallery ignores it
    // until release anyway. If the newest photo won't load, skip the slide and
    // let showGalleryPhoto()'s skip loop deal with it.
    int total = Storage::photoCount();
    int index = total > 0 ? Storage::photoAt(0) : 0;
    if (index > 0 && Storage::loadPhoto(index, galleryBits, sizeof(galleryBits))) {
        uint32_t start = millis();
        uint32_t slid;
        while ((slid = millis() - start) < SAVE_SLIDE_MS) {
            Display::drawGalleryTransition(galleryBits, 0, total,
                                           easeOutCubic((float)slid / SAVE_SLIDE_MS));
            Audio::update();
        }
    }
    showGalleryPhoto();
}

// Back to the viewfinder. Also called mid-hold, and the viewfinder's own
// press-edge test likewise waits for a release, so leaving never shoots.
static void leaveGallery() {
    Display::clearToast();
    Audio::playClick();  // Regular: back to the camera
    Serial.println("Gallery: close");
    // Slide back out like the save dismiss: the gallery photo rides the card
    // for the first half, the live viewfinder takes over from DISMISS_LIVE_AT
    // so the dither switch hides in the motion. The empty gallery got no
    // slide in, so it gets none out.
    if (Storage::photoCount() > 0) {
        uint32_t start = millis();
        uint32_t slid;
        while ((slid = millis() - start) < SAVE_SLIDE_MS) {
            float p = (float)slid / SAVE_SLIDE_MS;
            uint8_t* live = (p >= DISMISS_LIVE_AT) ? Camera::capture() : nullptr;
            Display::drawGalleryDismissTransition(live, Camera::WIDTH, Camera::HEIGHT,
                                                  galleryOrdinal, Storage::photoCount(),
                                                  1.0f - easeOutCubic(p));
            Audio::update();
        }
    }
    mode = Mode::Viewfinder;
    lastActivityTime = millis();
    hintToastShowing = false;
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
    // Pairing is the phone's moment, not an idle camera: it runs on its own
    // timeout in the mode switch below. An update is the same, only longer —
    // falling asleep mid-image would take the radio down under the transfer.
    if (mode != Mode::Asleep && mode != Mode::Pairing && mode != Mode::Updating &&
        now - lastActivityTime >= idleLimit) {
        enterAsleep();
    }

    Audio::update();
    Sync::loop();
    handleSyncEvents();

    // This image got through boot and has been running the loop for a while:
    // end the trial, so the next reset is an ordinary one. A no-op unless the
    // camera is actually running something the phone sent.
    if (Ota::trial() && now >= OTA_CONFIRM_MS) Ota::confirm();

    // USB console: an export in progress is activity. It holds off sleep,
    // and keeps a sleeping camera's radio on, without waking the screen.
    if (Console::poll()) {
        lastActivityTime = millis();
        asleepSince = millis();
    }

    // Toggle VCOM to prevent LCD burn-in. Every mode needs this.
    Display::refresh();

    // The mood is serviced every iteration; a chirp only plays on a screen it
    // may interrupt, with the button up (a 300ms chirp must not delay a
    // shutter press), nothing else sounding and no transfer running.
    serviceMood((mode == Mode::Viewfinder || mode == Mode::Asleep) && !pressed && !Audio::isPlaying() &&
                    !Sync::busy(),
                mode == Mode::Asleep ? "Asleep" : "Viewfinder");

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
                    case Storage::Result::Ok:
                        moodOnPhoto();
                        Audio::playMelody(Audio::Melody::Saved);
                        startDismiss("saved");
                        break;
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
            } else if (!pressed && lastPressed && gestureActive) {
                // Released before the hold: a tap. Answered with a chirp in
                // the camera's current mood, so an accidental press explains
                // itself and says how the camera is doing. Not activity: it
                // must not stretch the radio window.
                playMoodChirp(moodHappiness(), "Asleep tap");
            }
            if (!pressed) gestureActive = false;

            // The face breathes: the other frame every BREATH_MS.
            if (millis() - sleepFaceDrawnAt >= BREATH_MS) breatheSleepFace();

            // Radio window over? A phone that is busy with us extends it; so
            // does console traffic (above). Fresh millis(): asleepSince may
            // have been set later in this very iteration than `now`.
            if (millis() - asleepSince >= RADIO_LINGER_MS && !Sync::activeRecently(millis())) {
                enterSleepMode();
                return;  // After wake, restart loop fresh
            }
            break;
        }

        case Mode::Pairing: {
            // The button does nothing here; the phone drives. The screen ends
            // on the result (after a beat to read it) or on the timeout.
            const uint32_t waited = pairingDoneAt ? (millis() - pairingDoneAt) : (millis() - pairingStartedAt);
            if (waited >= (pairingDoneAt ? PAIRING_RESULT_MS : PAIRING_TIMEOUT_MS)) leavePairing();
            break;
        }

        case Mode::Updating: {
            // The phone drives; the button does nothing. Progress arrives as
            // events, and either a reboot or an outcome ends this screen.
            if (updateEndedAt && millis() - updateEndedAt >= UPDATE_RESULT_MS) leaveUpdating();
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
