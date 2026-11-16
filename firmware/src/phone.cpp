// What a phone can do to the screen: pairing, prompts and firmware updates.

#include "app.h"

namespace App {

// Pairing: where to go back to, when it started, and when it ended (0 = still
// waiting on the phone).
Mode modeBeforePairing = Mode::Viewfinder;
uint32_t pairingStartedAt = 0;
// The hello is the owner's to dismiss, on the phone or on the button, so it does not end on a beat.
bool helloShowing = false;
uint32_t pairingDoneAt = 0;
// Updating: where to go back to if it does not end in a reboot, the percent
// on screen, and when the outcome went up (0 = still running).
Mode modeBeforeUpdate = Mode::Viewfinder;
int updatePercent = -1;
uint32_t updateEndedAt = 0;
// A failure restarts the camera rather than dropping back to the viewfinder.
// Nothing is wrong with the image it is running — the half-written slot is —
// and coming back up clean is both what the screen promises and the cheapest
// way to be sure the flash writer let go of everything.
bool updateFailed = false;

// The passkey takes the whole screen. As a toast it was both too small to read
// out and too easy to lose: the viewfinder's own "press to shoot" hint painted
// straight over it. The camera does not wake for this — pairing needs the
// radio and the panel, not the sensor — so a sleeping camera pairs and goes
// back to its face.
// The pairing screen takes the panel from wherever the camera was, the sleep face included, and
// leavePairing() hands it back.
void takeOverForPairing() {
    if (mode != Mode::Pairing) modeBeforePairing = mode;
    Display::clearToast();
    hintToastShowing = false;
    savedFrame = nullptr;
    captureHeld = false;
    gestureActive = false;
    holdFired = false;
    mode = Mode::Pairing;
}

void enterPairing(uint32_t code) {
    helloShowing = false;
    char text[16];
    snprintf(text, sizeof(text), "%06lu", (unsigned long)code);
    takeOverForPairing();
    pairingStartedAt = millis();
    pairingDoneAt = 0;
    Display::drawPairing(text, "type this on your phone");
    Serial.printf("Pairing: code %s\n", text);
}

// Back to whatever was up before. A photo under review is not worth restoring
// through a pairing dialog, so anything that was not the sleep face resumes as
// the viewfinder.
void leavePairing() {
    helloShowing = false;
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
void enterUpdating(uint32_t size) {
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
    updateFailed = false;
    updatePercent = 0;
    Display::drawUpdate("freshing up", 0, "keep the phone close");
    Serial.printf("Update: receiving %u bytes, fw now %s\n", (unsigned)size, LC_VERSION_STRING);
}

// Back to whatever was up before, after an update that did not end in a
// reboot. A photo under review is long gone; anything that was not the sleep
// face resumes as the viewfinder, exactly as pairing does.
void leaveUpdating() {
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
void finishUpdating() {
    Serial.println("Update: verified and armed — restarting into it");
    Display::drawUpdate("brb", -1, "it will only be a minute i promise");
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
void failUpdating(uint32_t status) {
    if (mode != Mode::Updating) return;
    updateFailed = status != SyncProto::STATUS_ABORTED;
    Serial.printf("Update: %s (status %u)\n", updateFailed ? "failed" : "stopped", (unsigned)status);
    if (updateFailed) {
        Display::drawUpdate("ugh fuck", -1, "that didn't work, restarting", UI::getAlertIcon());
    } else {
        Display::drawUpdate("update stopped", -1, "nothing changed");
    }
    Audio::playMelody(Audio::Melody::DaDaTa);
    updateEndedAt = millis();
}

// The phone is waiting for one particular photo: the card that binds this
// camera to a blog, or the picture that heads it (docs/protocol.md §3.3). The
// camera says which, because the person holding it is looking at the camera,
// not at the phone. A sleeping camera wakes for it — being asked for a photo is
// the one message that is useless behind a closed face.
void enterPrompt(uint8_t kind) {
    // Hello is not something to wait for, it is something to say: the same screen the passkey ends on,
    // and the only greeting the camera can give a phone it is already bonded to.
    if (kind == SyncProto::PROMPT_HELLO) {
        takeOverForPairing();
        pairingDoneAt = millis();
        helloShowing = true;
        Display::drawPairing("oh hi", nullptr);
        Serial.println("Sync: saying oh hi");
        return;
    }
    if (kind == SyncProto::PROMPT_NONE && helloShowing) {
        leavePairing();
        return;
    }
    const Display::Prompt want = kind == SyncProto::PROMPT_SELFIE ? Display::Prompt::Selfie : Display::Prompt::None;
    Display::setPrompt(want);
    if (want == Display::Prompt::None) {
        Display::clearToast();
        hintToastShowing = false;
        return;
    }
    if (mode == Mode::Asleep) wakeFromAsleep();
    if (mode != Mode::Viewfinder) return;
    // Indefinite: it stands until the shot is taken or the phone takes it back,
    // unlike the "press to shoot" hint it replaces.
    Display::showToast("take one of you", Display::ToastHAlign::Center, Display::ToastVAlign::Bottom, true, 0);
    hintToastShowing = true;
    lastActivityTime = millis();
}

// Sync raises its events from loop() on the main thread, so drawing here is
// safe. Only pairing has a screen: connecting and transferring are the phone's
// business, and it shows its own progress.
void handleSyncEvents() {
    Sync::Event ev;
    while (Sync::nextEvent(&ev)) {
        switch (ev.kind) {
            case Sync::Event::ClockSet:
                // Never a screen change: the mood just learns the time.
                moodOnClock((ev.value & Sync::CLOCK_TZ_KNOWN) != 0, (int16_t)(uint16_t)(ev.value & 0xFFFF));
                break;
            case Sync::Event::Connected:
                Serial.println("Sync: phone connected");
                break;
            case Sync::Event::Disconnected:
                Serial.println("Sync: phone gone");
                break;
            case Sync::Event::Passkey:
                enterPairing(ev.value);
                break;
            case Sync::Event::Chirp:
                playMoodChirp(moodHappiness(), "Asked", (int)ev.value);
                break;
            case Sync::Event::Prompt:
                enterPrompt((uint8_t)ev.value);
                break;
            case Sync::Event::PairingDone:
                Serial.printf("Sync: link %s\n", ev.value ? "encrypted" : "NOT encrypted");
                // Only the screen that asked for the passkey: the link re-authenticates on every
                // connection, and the hello a phone wants said is PROMPT_HELLO, not this.
                if (mode == Mode::Pairing && !helloShowing) {
                    pairingDoneAt = millis();
                    Display::drawPairing(ev.value ? "oh hi" : "not this time", nullptr);
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

}  // namespace App
