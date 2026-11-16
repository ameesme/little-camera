/**
 * little-camera on the v3q board (ESP32-S3-WROOM-1-N16R8).
 *
 * Viewfinder + capture + sleep app. There is no modem: the phone is the
 * uplink. Pin map is the v3q netlist; see AGENTS.md.
 */

#include "app.h"

namespace App {

// State variables
Mode mode = Mode::Viewfinder;
bool lastPressed = false;
uint32_t lastActivityTime = 0;
bool hintToastShowing = false;
uint32_t captureShownAt = 0;
// The frame being reviewed. Camera::capture() hands back the driver's
// framebuffer and only recycles it on the next call, so this stays valid as
// long as Capture/Save never grab a new frame — which they don't.
uint8_t* savedFrame = nullptr;
// Save-screen gesture tracking: when the current press began, whether a press
// is in progress, and whether it already crossed the hold threshold.
uint32_t pressStartedAt = 0;
bool gestureActive = false;
bool holdFired = false;
// Dismiss slide: when it started and the toast to raise once it lands.
uint32_t dismissStartedAt = 0;
const char* pendingToast = nullptr;
// Grab-on-press, commit-on-release: the shutter still fires the instant it is
// pressed (that's what a camera button does), but whether the frozen frame
// goes on to the save screen or gets thrown away for the gallery is decided by
// whether the button is still down at GALLERY_HOLD_MS. True from the press
// edge until that decision is made.
bool captureHeld = false;
// Gallery: which photo (0 = newest), whether the screen needs redrawing, and
// what the toast looked like at the last redraw — the gallery is static, so it
// only repaints when something about it changed.
int galleryOrdinal = 0;
bool galleryDirty = false;
bool galleryToastWasVisible = false;
// Same for the save screen, which is also drawn once and then left alone.
bool saveToastWasVisible = false;
// The decoded photo. Its own buffer rather than Display's: 9.6KB is noise next
// to the 77KB camera frame, and Display keeps its bitmaps-in, pixels-out
// contract (which is what lets the preview tool render the gallery).
uint8_t galleryBits[Display::PHOTO_BYTES * Display::PHOTO_HEIGHT];
uint8_t offeredPal = 0;
uint32_t palShownAt = 0;
uint32_t palDrawnAt = 0;
bool palToastWasVisible = false;
// Frames drawn since palFpsSince, logged so the rate can be checked on a board.
uint32_t palFrames = 0;
uint32_t palFpsSince = 0;
// Opened from the menu to test the screen: it goes back to the menu afterwards
// instead of sliding out a save layout that has no photo behind it.
bool palFromMenu = false;
uint32_t palWelcomeAt = 0;
// Read once on entry: the face should not change expression mid-welcome.
uint8_t palWelcomeHappiness = 255;
bool cameraReady = false;
bool radioOn = false;

// Boot/wake gate, not a debounce: light sleep wakes on the pressed level, i.e.
// with the button still held, so the loop would otherwise see it as a fresh
// press. Block until the line has been continuously released for stableMs.
void waitForStableRelease(uint32_t stableMs) {
    uint32_t stableStart = millis();
    while (millis() - stableStart < stableMs) {
        if (shutterPressed()) {
            stableStart = millis();  // Bounce or still held — restart the window
        }
        delay(5);
    }
}

// The only debounce. Resampling over ~10ms rejects a noisy single sample.
// Measured on this switch: real presses hold ~200ms, release bounce lasts
// ~1ms — 10ms sits comfortably between the two, so no additional edge-timing
// window is needed. R26/C24 soften the edge but don't remove that bounce.
bool confirmPressed() {
    for (int i = 0; i < 5; i++) {
        delay(2);
        if (!shutterPressed()) return false;
    }
    return true;
}

// The camera rail is the biggest load on the board — 105mA with the sensor up
// against 62mA without — so every screen that isn't live preview drops it and
// pays a re-init when frames are wanted again. Camera::deinit() frees the
// framebuffer, so dropping the reviewed frame is part of switching off rather
// than something each caller has to remember.
bool cameraOn() {
    if (!cameraReady) {
        cameraReady = Camera::init();
        if (!cameraReady) Serial.println("Camera init failed");
    }
    return cameraReady;
}

void cameraOff() {
    if (!cameraReady) return;
    Camera::deinit();
    cameraReady = false;
    savedFrame = nullptr;
}

// Ease-out cubic. The panel only manages ~10 frames across a slide, and at that
// frame count a linear ramp reads as a mechanical stutter — decelerating into
// the resting layout hides it. Both slides end at rest, so both use it: the
// dismiss just runs the position backwards.
float easeOutCubic(float p) {
    float inv = 1.0f - p;
    return 1.0f - inv * inv * inv;
}

// Start sliding the save layout back out. The toast waits until the slide
// lands — raising it mid-slide would drop it on top of the columns still
// moving underneath it.
void startDismiss(const char* toast) {
    Display::clearToast();
    pendingToast = toast;
    // The second half of the slide wants live frames, and the sensor has been
    // off since the Save screen went up. The stall lands on the button press,
    // next to the flash write, rather than mid-animation.
    cameraOn();
    dismissStartedAt = millis();
    mode = Mode::Dismissing;
}

// A save with no pal equipped offers one. A mode rather than a blocking loop so
// the radio, toasts and the idle timeout keep running; walking away counts as
// a skip, and the next photo offers again.
void enterPalFound() {
    palFromMenu = false;
    offeredPal = 1 + (uint8_t)(esp_random() % Pal::COUNT);
    Serial.printf("Pal: offering %u\n", offeredPal);
    Audio::playMelody(Audio::Melody::PalFound);
    Display::clearToast();
    gestureActive = false;
    holdFired = false;
    lastActivityTime = millis();
    palShownAt = palDrawnAt = palFpsSince = millis();
    palFrames = 0;
    Display::drawPalDialog(offeredPal, 0);
    palToastWasVisible = Display::toastVisible();
    mode = Mode::PalFound;
}

void leavePalFound(const char* toast) {
    if (!palFromMenu) {
        startDismiss(toast);
        return;
    }
    mode = Mode::Menu;
    gestureActive = false;
    holdFired = false;
    lastActivityTime = millis();
    // No toast: the menu never repaints when one expires, so it would stick.
    showMenu();
}

void enterPalWelcome() {
    Audio::playMelody(Audio::Melody::Saved);
    Display::clearToast();
    gestureActive = false;
    holdFired = false;
    lastActivityTime = millis();
    palWelcomeAt = palDrawnAt = millis();
    palWelcomeHappiness = moodHappiness();
    Display::drawPalWelcome(palWelcomeHappiness, palState.id, 0.0f, 0, true);
    mode = Mode::PalWelcome;
}

// From a save, the welcome face is the awake face, so it hands over to the
// viewfinder exactly as a wake does: the face slides up and its miniature
// settles into the sidebar. From the menu test it just goes back to the menu.
void leavePalWelcome() {
    if (palFromMenu) {
        leavePalFound(nullptr);
        return;
    }
    cameraOn();
    enterWakeViewfinder();
    returnToViewfinder();
}

// Draw the current gallery photo. Files that fail to decode are skipped — a
// partial write can't leave one behind (savePhoto removes them), but a file
// copied onto the flash by some future tool might be the wrong size.
void showGalleryPhoto() {
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
void enterGallery() {
    cameraOff();
    captureHeld = false;
    galleryOrdinal = 0;
    gestureActive = false;
    holdFired = false;
    Display::clearToast();
    hintToastShowing = false;
    galleryToastWasVisible = false;
    mode = Mode::Gallery;
    lastActivityTime = millis();
    Serial.println("Gallery: open");
    int total = Storage::photoCount();
    int index = total > 0 ? Storage::photoAt(0) : 0;
    if (index > 0 && Storage::loadPhoto(index, galleryBits, sizeof(galleryBits))) {
        uint32_t start = millis();
        uint32_t slid;
        while ((slid = millis() - start) < SAVE_SLIDE_MS) {
            if (shutterPressed() && (millis() - pressStartedAt) >= MENU_HOLD_MS) {
                enterMenu();
                return;
            }
            Display::drawGalleryTransition(galleryBits, 0, total,
                                           easeOutCubic((float)slid / SAVE_SLIDE_MS));
            Audio::update();
        }
    }
    // The button may still be held from the viewfinder — wait for menu or release.
    while (shutterPressed()) {
        if ((millis() - pressStartedAt) >= MENU_HOLD_MS) {
            enterMenu();
            return;
        }
        delay(5);
    }
    showGalleryPhoto();
}

// Back to the viewfinder. Also called mid-hold, and the viewfinder's own
// press-edge test likewise waits for a release, so leaving never shoots.
void leaveGallery() {
    Display::clearToast();
    cameraOn();          // The slide out hands over to live frames halfway
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
    gestureActive = false;
    lastActivityTime = millis();
    hintToastShowing = false;
}

// Slide finished: back to live preview, with a toast naming what happened.
void returnToViewfinder() {
    savedFrame = nullptr;
    mode = Mode::Viewfinder;
    gestureActive = false;
    lastActivityTime = millis();
    hintToastShowing = false;
    Display::clearToast();
    if (pendingToast) {
        Display::showToast(pendingToast, Display::ToastHAlign::Right,
                           Display::ToastVAlign::Top);
        pendingToast = nullptr;
    }
}

}  // namespace App

using namespace App;

void setup() {
    // Plain INPUT: R26 is the pull-up, so the internal one would only fight
    // the RC. Buzzer gate low before anything else can float it.
    pinMode(PIN_SHUTTER, INPUT);
    pinMode(PIN_BUZZ, OUTPUT);
    digitalWrite(PIN_BUZZ, LOW);
    pinMode(PIN_VBUS, INPUT);
    pinMode(PIN_CHG, INPUT);
    lastUsb = onUsb();

    Serial.begin(115200);
    // Never let a print wait. After a light sleep the USB-Serial-JTAG has
    // dropped off the bus while the host still polls the port, so the core
    // believes it is connected with nobody reading: every print then blocks up
    // to 2 s for ring space. With 0 a full buffer drops the text instead.
    Serial.setTxTimeoutMs(0);
    uint32_t serialWait = millis();
    while (!Serial && millis() - serialWait < 2000) delay(10);
    Serial.println("little-camera init — v3q");
    // Why are we booting? A splash where a sleep face was expected means a
    // reset; this line says which kind (1 power-on, 3 software, 4 panic,
    // 5 interrupt watchdog, 6 task watchdog, 9 brownout, per esp_reset_reason_t).
    Serial.printf("Reset reason: %d\n", (int)esp_reset_reason());
    // Expected to be 0: the part is populated but platformio.ini probes it in
    // quad mode on purpose so it is never enabled. Anything else means PSRAM
    // came up and is costing self-refresh through every light sleep.
    Serial.printf("PSRAM: %u KB\n", (unsigned)(ESP.getPsramSize() >> 10));

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

    // Photo storage. Non-fatal: the camera still works without it, saves just
    // report an error, and refusing to boot over a bad partition would be worse.
    Storage::init();
    Display::setShowHints(Storage::photoCount() < 2);

    Identity::init();
    // Before the melody: a muted camera keeps its mute through a reboot, chime included.
    moodBoot();
    applyMute();
    palBoot();
    Display::setPal(palState.id);
    palShowCare();
    Audio::playMelody(Audio::Melody::TaDaDa);

    // Play melody during splash (non-blocking update loop)
    uint32_t splashStart = millis();
    while (millis() - splashStart < 1500) {
        Audio::update();
        delay(10);
    }

    Console::begin();
    completeWakeFace(false);

    // Initialize camera (OV2640 on J2)
    // Non-fatal, like storage above: cameraOn() tries again every time the viewfinder wants a
    // frame, and halting here would take the radio and the console down with it — leaving no way
    // to reach the photos already on flash, which is the one thing worth protecting.
    if (cameraOn()) enterWakeViewfinder();
    else Serial.println("Camera init failed — carrying on without it");

    // Radio last: everything it advertises (photo counts, the secret) exists
    // by now, and the camera init above is the slow part of boot anyway.
    // -DLC_NO_BLE in platformio.ini builds without the radio, to tell a BLE
    // problem from everything else.
#ifndef LC_NO_BLE
    Sync::setTelemetry(readTelemetry);
#endif

    // Don't enter the loop until the shutter line is quiet
    waitForStableRelease(50);

    // Initialize activity timer
    lastActivityTime = millis();
}

void loop() {
    bool pressed = shutterPressed();
    uint32_t now = millis();

    // Idle timeout: sleep face up and radio still on first, light sleep later
    // from the Asleep case below. Also applies on the save screen: an
    // unanswered prompt is still an idle device.
    const uint32_t idleLimit =
        Display::promptPending()                      ? PROMPT_IDLE_TIMEOUT_MS
        : (mode == Mode::Gallery || mode == Mode::Save || mode == Mode::Menu ||
           mode == Mode::PalFound)                    ? REVIEW_IDLE_TIMEOUT_MS
                                                        : IDLE_TIMEOUT_MS;
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

    serviceUsb(mode == Mode::Asleep);

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
    if (mode == Mode::Viewfinder) moodHappiness();

    switch (mode) {
        case Mode::Viewfinder: {
#ifndef LC_NO_BLE
            if (onUsb() && !radioOn) {
                Sync::begin(); radioOn = true;
            } else if (!onUsb() && radioOn && !Sync::connected()) {
                Sync::end(); radioOn = false;
            }
#endif

            if (!hintToastShowing && Storage::photoCount() < 2 &&
                (now - lastActivityTime) >= HINT_TOAST_DELAY_MS) {
                Display::showToast("press to shoot", Display::ToastHAlign::Right,
                                   Display::ToastVAlign::Top, false, 0);
                hintToastShowing = true;
            }

            if (!gestureActive) {
#ifdef LC_PROFILE_VIEWFINDER
                const uint32_t captureStart = micros();
#endif
                uint8_t* frame = Camera::capture();
#ifdef LC_PROFILE_VIEWFINDER
                const uint32_t captureUs = micros() - captureStart;
#endif
                if (frame) {
                    Display::drawViewfinder(frame, Camera::WIDTH, Camera::HEIGHT);
#ifdef LC_PROFILE_VIEWFINDER
                    static uint32_t start = captureStart, frames = 0;
                    static uint32_t captureSum = 0, ditherSum = 0, layoutSum = 0, spiSum = 0, captureMax = 0;
                    static uint32_t photoSum = 0, sidebarSum = 0, overlaySum = 0;
                    const auto timing = Display::viewfinderTiming();
                    captureSum += captureUs;
                    ditherSum += timing.ditherUs;
                    layoutSum += timing.layoutUs;
                    spiSum += timing.spiUs;
                    photoSum += timing.photoUs;
                    sidebarSum += timing.sidebarUs;
                    overlaySum += timing.overlayUs;
                    if (captureUs > captureMax) captureMax = captureUs;
                    frames++;
                    const uint32_t elapsed = micros() - start;
                    // Aggregate before logging so USB output never sits in the per-frame timings.
                    if (elapsed >= 1000000) {
                        Serial.printf("FPS: n=%lu fps=%.2f capture=%.3f dither=%.3f layout=%.3f spi=%.3f capture_max=%.3f photo=%.3f sidebar=%.3f overlay=%.3f ms\n",
                                      (unsigned long)frames, frames * 1000000.0 / elapsed,
                                      captureSum / (frames * 1000.0), ditherSum / (frames * 1000.0),
                                      layoutSum / (frames * 1000.0), spiSum / (frames * 1000.0), captureMax / 1000.0,
                                      photoSum / (frames * 1000.0), sidebarSum / (frames * 1000.0), overlaySum / (frames * 1000.0));
                        start = micros();
                        frames = captureSum = ditherSum = layoutSum = spiSum = captureMax = 0;
                        photoSum = sidebarSum = overlaySum = 0;
                    }
#endif
                }
            }

            if (pressed && !lastPressed) {
                pressStartedAt = millis();
                gestureActive = true;
                holdFired = false;
            } else if (pressed && gestureActive) {
                const uint32_t held = millis() - pressStartedAt;
                if (held < GALLERY_HOLD_MS) {
                    Display::drawHoldProgress(held, GALLERY_HOLD_MS);
                } else if (!holdFired) {
                    Display::completeHold();
                    holdFired = true;
                    Audio::playClick(true);
                    enterGallery();
                    break;
                }
            } else if (!pressed && lastPressed && gestureActive) {
                gestureActive = false;
                Display::drawHoldProgress(0, GALLERY_HOLD_MS);
                lastActivityTime = millis();
                Serial.println("Shutter — capturing");
                Display::clearToast();
                hintToastShowing = false;
                savedFrame = Camera::capture();
                if (savedFrame) {
                    Display::drawCapture(savedFrame, Camera::WIDTH, Camera::HEIGHT);
                    Audio::playClick();
                    Audio::playMelody(Audio::Melody::TaDaDa);
                    captureShownAt = millis();
                    mode = Mode::Capture;
                }
            }
            if (!pressed) gestureActive = false;
            break;
        }

        case Mode::Capture: {
            uint32_t elapsed = millis() - captureShownAt;
            if (elapsed < CAPTURE_DWELL_MS) break;

            uint32_t slid = elapsed - CAPTURE_DWELL_MS;
            if (slid < SAVE_SLIDE_MS) {
                Display::drawSaveTransition(easeOutCubic((float)slid / SAVE_SLIDE_MS));
                break;
            }

            Display::clearToast();
            saveToastWasVisible = false;
            Display::drawSave(savedFrame, Camera::WIDTH, Camera::HEIGHT);
            mode = Mode::Save;
            cameraOff();
            gestureActive = false;
            holdFired = false;
            break;
        }

        case Mode::Save: {
            Display::drawHoldProgress(pressed && gestureActive && !holdFired ? millis() - pressStartedAt : 0,
                                      TRASH_HOLD_MS);
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
                Display::completeHold();
                holdFired = true;
                Audio::playMelody(Audio::Melody::DaDaTa);
                startDismiss("deleted");
            } else if (!pressed && lastPressed && gestureActive && !holdFired) {
                Display::flashPress();
                // Released before the threshold: a tap. No modem on this board,
                // so "send" writes the photo to flash and stops there
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
                        Storage::bumpShotCount();
                        moodOnPhoto();
                        // Whatever the phone was waiting for, it has it now.
                        Display::setPrompt(Display::Prompt::None);
                        // No pal, or only its tombstone: offer one.
                        if (!Pal::alive(palState)) {
                            enterPalFound();
                        } else if (Pal::onPhoto(palState, moodRng)) {
                            palPersist("found food");
                            palShowCare();
                            // The melody says it saved; the toast is only for what turned up.
                            static char foundToast[24];
                            snprintf(foundToast, sizeof foundToast, "found a %s!", Display::palFoodName(palState.id));
                            Serial.printf("Pal: found a %s, %u in hand\n", Display::palFoodName(palState.id), palState.food);
                            Audio::playMelody(Audio::Melody::Saved);
                            startDismiss(foundToast);
                        } else {
                            Audio::playMelody(Audio::Melody::Saved);
                            startDismiss(nullptr);
                        }
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
                    // Not drawSave(): the camera is off and its framebuffer is
                    // gone. t=1 is the same layout off the cached dither.
                    Display::drawSaveTransition(1.0f);
                    Display::drawHoldProgress(pressed && gestureActive ? millis() - pressStartedAt : 0,
                                              TRASH_HOLD_MS);
                }
            }
            break;
        }

        case Mode::Gallery: {
            Display::drawHoldProgress(pressed && gestureActive && !holdFired ? millis() - pressStartedAt : 0,
                                      GALLERY_HOLD_MS);
            if (pressed && !lastPressed && confirmPressed()) {
                pressStartedAt = millis();
                gestureActive = true;
                holdFired = false;
                lastActivityTime = pressStartedAt;  // Browsing is activity
            } else if (pressed && gestureActive && !holdFired &&
                       (millis() - pressStartedAt) >= GALLERY_HOLD_MS) {
                // Fires on the threshold, like trash, so the hold has an end.
                Display::completeHold();
                holdFired = true;
                leaveGallery();
                break;
            } else if (!pressed && lastPressed && gestureActive && !holdFired) {
                Display::flashPress();
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
                Display::drawHoldProgress(pressed && gestureActive ? millis() - pressStartedAt : 0,
                                          GALLERY_HOLD_MS);
            }
            break;
        }

        case Mode::Menu: {
            Display::drawHoldProgress(pressed && gestureActive && !holdFired ? millis() - pressStartedAt : 0,
                                      GALLERY_HOLD_MS);
            if (pressed && !lastPressed) {
                pressStartedAt = millis();
                gestureActive = true;
                holdFired = false;
                lastActivityTime = millis();
            } else if (pressed && gestureActive && !holdFired &&
                       (millis() - pressStartedAt) >= GALLERY_HOLD_MS) {
                Display::completeHold();
                holdFired = true;
                menuSelect();
                break;
            } else if (!pressed && lastPressed && gestureActive && !holdFired) {
                Display::flashPress();
                int prev = menuSelected;
                menuSelected = (menuSelected + 1) % MENU_COUNT;
                Audio::playClick(true);
                Display::drawMenuSelect(MENU_ITEMS, MENU_COUNT, prev, menuSelected);
            }
            if (!pressed) gestureActive = false;
            break;
        }

        case Mode::Asleep: {
            if (pressed && !lastPressed && confirmPressed()) {
                Display::clearWakeProgress();
                pressStartedAt = millis();
                gestureActive = true;
            } else if (pressed && gestureActive) {
                Display::drawWakeProgress(millis() - pressStartedAt, WAKE_HOLD_MS);
                if ((millis() - pressStartedAt) >= WAKE_HOLD_MS) {
                    completeWakeFace();
                    wakeFromAsleep(true);
                    break;
                }
            } else if (!pressed && lastPressed && gestureActive) {
                const uint32_t held = millis() - pressStartedAt;
                Display::clearWakeProgress();
                showSleepFace(true);
                if (held <= SLEEP_TAP_MAX_MS) sleepTap("Asleep tap");
            }
            if (!pressed) gestureActive = false;

            // The face breathes: the other frame every BREATH_MS.
            if (!gestureActive && millis() - sleepFaceDrawnAt >= BREATH_MS) breatheSleepFace();
            // The pal idles while the chip is up; the wake fill owns the panel during a hold.
            if (!gestureActive) Display::animateSleepPal(millis());

            // On USB power: stay awake indefinitely, keep the radio on.
            if (onUsb()) {
                asleepSince = millis();
                break;
            }
            if (!gestureActive && millis() - asleepSince >= RADIO_LINGER_MS && !Sync::activeRecently(millis())) {
                enterSleepMode();
                return;
            }
            break;
        }

        case Mode::Pairing: {
            // The button does nothing here; the phone drives. The screen ends
            // on the result (after a beat to read it) or on the timeout.
            // The hello is the exception: it waits for the owner, on either device, and only the
            // pairing timeout stands behind it so a camera left alone still gets to sleep.
            if (helloShowing) {
                if (pressed && !lastPressed) leavePairing();
                if (millis() - pairingDoneAt >= PAIRING_TIMEOUT_MS) leavePairing();
                break;
            }
            const uint32_t waited = pairingDoneAt ? (millis() - pairingDoneAt) : (millis() - pairingStartedAt);
            if (waited >= (pairingDoneAt ? PAIRING_RESULT_MS : PAIRING_TIMEOUT_MS)) leavePairing();
            break;
        }

        case Mode::Updating: {
            // The phone drives; the button does nothing. Progress arrives as
            // events, and either a reboot or an outcome ends this screen.
            if (updateEndedAt && millis() - updateEndedAt >= UPDATE_RESULT_MS) {
                if (updateFailed) esp_restart();
                leaveUpdating();
            }
            break;
        }

        case Mode::PalFound: {
            if (pressed && !lastPressed && confirmPressed()) {
                pressStartedAt = millis();
                gestureActive = true;
                holdFired = false;
            } else if (pressed && gestureActive && !holdFired &&
                       (millis() - pressStartedAt) >= GALLERY_HOLD_MS) {
                Serial.println("Pal: skipped");
                Display::completeHold();
                holdFired = true;
                Audio::playClick(true);
                leavePalFound(nullptr);
            } else if (!pressed && lastPressed && gestureActive && !holdFired) {
                Display::flashPress();
                Audio::playClick();
                Pal::equip(palState, moodClock(), offeredPal);
                palPersist("equipped");
                Display::setPal(palState.id);
                palShowCare();
                Serial.printf("Pal: equipped %u\n", palState.id);
                enterPalWelcome();
            }
            if (!pressed) gestureActive = false;

            if (mode == Mode::PalFound) {
                Display::drawHoldProgress(pressed && gestureActive && !holdFired ? millis() - pressStartedAt : 0,
                                          GALLERY_HOLD_MS);
                const bool toastNow = Display::toastVisible();
                if (toastNow != palToastWasVisible) {
                    // A toast lives outside the badge's rows: only a full frame adds or removes it.
                    palToastWasVisible = toastNow;
                    palDrawnAt = millis();
                    Display::drawPalDialog(offeredPal, palDrawnAt - palShownAt);
                } else if (millis() - palDrawnAt >= PAL_FRAME_MS) {
                    // Stepped from the last due time rather than now, so a slow frame doesn't drift the rate.
                    palDrawnAt += PAL_FRAME_MS;
                    if (millis() - palDrawnAt >= PAL_FRAME_MS) palDrawnAt = millis();
                    Display::drawPalDialogFrame(offeredPal, millis() - palShownAt);
                    palFrames++;
                }
                if (millis() - palFpsSince >= 5000) {
                    Serial.printf("Pal: %lu fps\n", (unsigned long)(palFrames * 1000 / (millis() - palFpsSince)));
                    palFrames = 0;
                    palFpsSince = millis();
                }
            }
            break;
        }

        case Mode::PalWelcome: {
            const uint32_t shown = millis() - palWelcomeAt;
            if ((pressed && !lastPressed && confirmPressed()) || shown >= PAL_WELCOME_MS) {
                if (shown < PAL_WELCOME_MS) Audio::playClick();
                leavePalWelcome();
                break;
            }
            if (millis() - palDrawnAt >= PAL_FRAME_MS) {
                palDrawnAt = millis();
                const float p = shown >= PAL_WELCOME_SLIDE_MS ? 1.0f : (float)shown / PAL_WELCOME_SLIDE_MS;
                Display::drawPalWelcome(palWelcomeHappiness, palState.id, easeOutCubic(p), shown, false);
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
