// The sleep face, light sleep and the way back to the viewfinder.

#include "app.h"

namespace App {

// When the sleep face went up (the radio window counts from here).
// Camera::deinit() is not idempotent, so the driver's state is tracked too.
uint32_t asleepSince = 0;

void completeWakeFace(bool fromHold) {
    const uint8_t happiness = moodHappiness();
    if (fromHold) Display::drawWakeProgress(WAKE_HOLD_MS, WAKE_HOLD_MS);
    Display::drawAwake(happiness);
    playMoodChirp(happiness, "Wake", 1);
}

void enterWakeViewfinder() {
    const uint8_t happiness = moodHappiness();
    Display::setFaceBand(Chirp::band(happiness));
    uint8_t* frame = Camera::capture();
    // A failed sensor must not animate a stale photo from before sleep.
    if (!frame) return;
    // Keep the face visible through sensor startup and capture so its exit leads straight to a real frame.
    const uint32_t exitStart = millis();
    for (;;) {
        const uint32_t elapsed = millis() - exitStart;
        const float t = elapsed >= WAKE_SLIDE_MS ? 1.0f : (float)elapsed / WAKE_SLIDE_MS;
        Display::drawWakeExit(happiness, easeOutCubic(t));
        if (t >= 1.0f) break;
        delay(1);
    }
    Display::drawWakeEnter(frame, Camera::WIDTH, Camera::HEIGHT, 0.0f);
    frame = nullptr;
    // Hold the cached first frame during the wake handoff so the miniature gets its own beat.
    delay(WAKE_FACE_DELAY_MS);
    const uint32_t start = millis();
    for (;;) {
        const uint32_t elapsed = millis() - start;
        const float t = elapsed >= WAKE_SLIDE_MS ? 1.0f : (float)elapsed / WAKE_SLIDE_MS;
        Display::drawWakeEnter(frame, Camera::WIDTH, Camera::HEIGHT, easeOutCubic(t));
        frame = nullptr;
        if (t >= 1.0f) break;
        delay(1);
    }
}

uint32_t heldToWake() {
    Display::clearWakeProgress();
    uint32_t start = millis();
    while (millis() - start < WAKE_HOLD_MS) {
        if (!shutterPressed()) {
            const uint32_t held = millis() - start;
            Display::clearWakeProgress();
            showSleepFace(true);
            return held;
        }
        Display::drawWakeProgress(millis() - start, WAKE_HOLD_MS);
        delay(10);
    }
    completeWakeFace();
    return WAKE_HOLD_MS;
}

// Idle timeout: sleep face up, camera driver down, radio still on. To the
// user this is sleep; to a phone it is another RADIO_LINGER_MS of chances to
// collect photos before the chip really goes down (enterSleepMode). A photo
// under review is discarded: walking away is not consent to keep it.
void enterAsleep() {
    Display::clearToast();
    hintToastShowing = false;
    pendingToast = nullptr;
    savedFrame = nullptr;
    captureHeld = false;
    Audio::playMelody(Audio::Melody::DaDaTa);
#ifndef LC_NO_BLE
    if (!radioOn) { Sync::begin(); radioOn = true; }
#endif
    Serial.println("Asleep (radio on)");
    cameraOff();
    sampleBattery();
    sleepBreathIn = false;
    showSleepFace(true);
    asleepSince = millis();
    gesture.active = false;
    gesture.fired = false;
    mode = Mode::Asleep;
}

// A one-second hold on the sleeping camera: back to the viewfinder, at the
// moment the hold completes rather than on release. The shutter is still
// down on the way out and the viewfinder acts on press edges only, so the
// hold does not shoot.
void wakeFromAsleep(bool fromHold) {
#ifndef LC_NO_BLE
    // Keep the radio if a phone is mid-session (it prompted this wake) or on USB
    if (radioOn && !Sync::connected() && !onUsb()) {
        Sync::end();
        radioOn = false;
    }
#endif
    Serial.println(radioOn ? "Woke (radio stays)" : "Woke");
    if (!fromHold) {
        Audio::init(PIN_BUZZ);
        Audio::playClick();
    }
    cameraOn();
    if (fromHold) enterWakeViewfinder();
    mode = Mode::Viewfinder;
    hintToastShowing = false;
    gesture.active = false;
    gesture.fired = false;
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
    captureHeld = false;

    // Sleep face and melody are normally already done by enterAsleep(); this
    // keeps the direct path (and the preview's expectations) intact.
    while (Audio::isPlaying()) {
        Audio::update();
        delay(10);
    }
    showSleepFace(true);
    // Light sleep stops the loop that animates the pal: leave it at rest, not mid-blink.
    Display::animateSleepPal(0);

    // Radio off before the camera: light sleep and a live BLE controller is
    // undefined territory in this Arduino core, and the phone is told nothing
    // — it reconnects when the camera advertises again after wake.
#ifndef LC_NO_BLE
    Sync::end();
    radioOn = false;
#endif

    // Drops the camera rails via CAM_EN, so the sensor costs nothing while we
    // are out — unlike the previous board, where it stayed biased at ~20mA
    // through sleep (docs/camera_standby.md). Costs a re-init on wake.
    // (Usually already done by enterAsleep(), which precedes this.)
    cameraOff();

    // Pressed is LOW on this board, so that is the level that wakes us.
    gpio_wakeup_enable((gpio_num_t)PIN_SHUTTER, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Small delay to let display finish and avoid immediate wake
    delay(100);

    // The panel keeps showing the sleep face while we're out, and DISP stays
    // HIGH across light sleep. On v3q the panel's VCOM comes from U8's
    // EXTCOMIN with EXTMODE tied high, so burn-in is handled in copper and no
    // longer depends on us waking up; the timer stays for the breath and the
    // mood's clock, and stretches to SLEEP_TICK_MS once the breath is over.
    //
    // Only the shutter or USB ends the sleep. Anything else — the BT
    // controller's own wake source if the radio was not fully down, a
    // rejected sleep call, an undefined cause — is not a person, and treating
    // it as one is exactly how the camera "wakes by itself".
    uint32_t breathUntil = millis() + BREATH_WINDOW_MS;
    bool usbWake = false;
    // VBUS is level-triggered: arming HIGH also catches a plug during sleep setup.
    gpio_wakeup_enable((gpio_num_t)PIN_VBUS, GPIO_INTR_HIGH_LEVEL);
    for (;;) {
        serviceUsb(true);
        if (onUsb()) {
            usbWake = true;
            break;
        }
        const bool breathing = (int32_t)(breathUntil - millis()) > 0;
        esp_sleep_enable_timer_wakeup(
            (uint64_t)(breathing ? Display::VCOM_INTERVAL_MS : SLEEP_TICK_MS) * 1000);
        esp_err_t err = esp_light_sleep_start();
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

        if (err != ESP_OK) {
            // Refused to sleep at all (a wake source already pending). The
            // cause is stale in that case, so don't read anything into it.
            Serial.printf("Sleep: rejected (%d), retrying\n", (int)err);
            delay(50);
            continue;
        }
        serviceUsb(true);
        if (onUsb()) {
            usbWake = true;
            break;
        }
        if (cause == ESP_SLEEP_WAKEUP_GPIO && shutterPressed()) {
            // The button — but a tap is not enough. Hold to wake; a release
            // before WAKE_HOLD_MS goes straight back to sleep.
            const uint32_t held = heldToWake();
            if (held >= WAKE_HOLD_MS) break;
            if (held <= SLEEP_TAP_MAX_MS) {
                sleepTap("Sleep tap");
                idlePalAfterTap();
            }
            breathUntil = millis() + BREATH_WINDOW_MS;
            continue;
        }
        if (cause == ESP_SLEEP_WAKEUP_TIMER) {
            // Keep-alive tick, and the mood's clock while asleep: pay a due
            // chirp. Radio and camera are down here, so a 300ms chirp costs
            // nothing but the sound. The VCOM flip is now a no-op on the panel
            // (EXTMODE is high) but stays so tools/preview keeps exercising the
            // same byte stream.
            serviceMood(true, "Sleep");
            if (breathing) {
                Display::toggleVcom();
                breatheSleepFace();
            } else {
                // Both, not ||: the battery sample has to be taken either way.
                const bool battery = sampleBattery();
                if (palShowCare() || battery) showSleepFace(true);
            }
            continue;
        }
        Serial.printf("Sleep: ignoring wake cause %d\n", (int)cause);
        delay(20);  // Don't spin hot if the source keeps firing
    }

    // Disable both wake sources after waking for real
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    gpio_wakeup_disable((gpio_num_t)PIN_SHUTTER);
    gpio_wakeup_disable((gpio_num_t)PIN_VBUS);

    if (usbWake) {
        // USB keeps the console and radio available without powering the sensor.
        mode = Mode::Asleep;
        asleepSince = millis();
        gesture.active = false;
        lastPressed = false;
#ifndef LC_NO_BLE
        Sync::begin();
        radioOn = true;
#endif
        return;
    }

    // Woke up! Reset activity timer and hint state
    Serial.println("Woke up from sleep!");
    lastActivityTime = millis();
    hintToastShowing = false;
    Display::clearToast();

    // Bring the sensor back up (torn down before sleep). On failure the loop
    // just gets nullptr frames from capture() and keeps a stale viewfinder,
    // which beats halting a device the user just woke up.
    cameraOn();
    enterWakeViewfinder();

#ifndef LC_NO_BLE
    if (onUsb()) { Sync::begin(); radioOn = true; }
#endif

    // The shutter is still held (that is what woke us), and the viewfinder
    // only acts on a press *edge*: marking it as already pressed means the
    // hold that woke the camera doesn't also shoot, without making the user
    // let go first. Release bounce is covered by confirmPressed() as usual.
    lastPressed = true;
}

}  // namespace App
