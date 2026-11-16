#pragma once

// Shared between the files main.cpp was split into. Everything here was a
// file-static of main.cpp; it is one program with one loop, so the state stays
// plain globals in a namespace rather than growing accessors nobody needs.

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
#include "pal.h"
#include "power.h"
#include "storage.h"
#include "sync.h"
#include "sync_protocol.h"
#include "version.h"
#include <Preferences.h>
#include <LittleFS.h>

namespace App {

// Sharp Memory LCD (LS027B7DH01) on J3, each line through a 330R guard
constexpr uint8_t PIN_LCD_SCLK = 4;
constexpr uint8_t PIN_LCD_MOSI = 5;   // SI — write-only bus, no MISO
constexpr uint8_t PIN_LCD_CS   = 6;   // SCS — ACTIVE HIGH
constexpr uint8_t PIN_LCD_DISP = 7;   // HIGH = panel shows

// Q2 (DMG1012UW) gate, low-side, buzzer to +3V3. Must idle LOW: HIGH turns
// the FET on, which puts near-DC through the coil.
constexpr uint8_t PIN_BUZZ    = 46;

// D2MQ, NO to GND, with R26 10k pull-up and C24 100n on the line, so the
// board does the pulling and pressed reads LOW. (The previous carrier had the
// switch's NC terminal routed, which inverted this.)
constexpr uint8_t PIN_SHUTTER = 44;

// Power sense. VBAT is halved by R34/R35 (1M/1M); VBUS and CHG are dividers
// read as logic, and CHG only means anything with VBUS present.
constexpr uint8_t PIN_VBAT = 3;
constexpr uint8_t PIN_VBUS = 16;
constexpr uint8_t PIN_CHG  = 17;

inline bool shutterPressed() {
    return digitalRead(PIN_SHUTTER) == LOW;
}

// Timing constants
constexpr uint32_t IDLE_TIMEOUT_MS = 20000;  // 20 seconds idle -> sleep
// Looking at a photo is slower than framing one: the gallery, and the save
// screen with a photo waiting on a decision, get longer before the sleep
// face comes up.
constexpr uint32_t REVIEW_IDLE_TIMEOUT_MS = 30000;
// A photo the phone is standing by for: long enough to find the card and frame it, short enough
// that a camera put down mid-ask still sleeps.
constexpr uint32_t PROMPT_IDLE_TIMEOUT_MS = 120000;
constexpr uint32_t HINT_TOAST_DELAY_MS = 5000;  // Show hint after 5s idle
// Pairing holds the screen until the phone is done with it. The timeout is the
// escape hatch for a phone that walks away mid-dialog; the result lingers just
// long enough to be read.
constexpr uint32_t PAIRING_TIMEOUT_MS = 60000;
constexpr uint32_t PAIRING_RESULT_MS = 6000;
// Between two chirps: long enough to be two and not a stutter, short enough to feel like one phrase.
constexpr uint32_t CHIRP_GAP_MIN_MS = 40;
constexpr uint32_t CHIRP_GAP_MAX_MS = 300;

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
// Keep holding past the gallery threshold and the system menu opens instead.
constexpr uint32_t MENU_HOLD_MS = 2000;

// After the idle timeout the sleep face goes up at once, but the radio keeps
// advertising behind it for this long so a phone can still collect photos.
// Only then does the chip light-sleep (radio off). A phone mid-transfer
// stretches the window.
constexpr uint32_t RADIO_LINGER_MS = 30000;
// Waking — with the radio still on or from light sleep — takes a hold, not a
// tap. A camera in a bag gets tapped; a second of pressure is intent.
constexpr uint32_t WAKE_HOLD_MS = 1000;
constexpr uint32_t SLEEP_TAP_MAX_MS = 300;
constexpr uint32_t WAKE_SLIDE_MS = 180;
constexpr uint32_t WAKE_FACE_DELAY_MS = 300;

// One button drives everything, so the UI is a strict mode machine: the
// meaning of a press depends entirely on which screen is up.
enum class Mode {
    Viewfinder,  // Live preview; press shoots, hold opens the gallery
    Capture,     // Frozen frame: dwell, then slide into Save
    Save,        // Frame plus send/trash; press sends, hold trashes
    Dismissing,  // Slide back out after an action, then resume the viewfinder
    Gallery,     // Stored photos, newest first; press = next, hold = back
    Menu,        // System menu; press = next item, hold = select
    Asleep,      // Sleep face up, radio still advertising; hold wakes
    Pairing,     // The phone is asking for the passkey; the code owns the screen
    Updating,    // New firmware coming in over BLE; progress owns the screen
    PalFound,    // "you got a little pal" after a save; press equips, hold skips
    PalWelcome,  // The awake face and the new pal sliding in; press or wait to go on
};
constexpr int MENU_COUNT = 9;
// One slot that reads "unmute" while muted, so the list and its indices never shift.
constexpr int MENU_MUTE = 6;

// Companion pal — persisted in NVS("pal"): its care as a blob under "care",
// and under "id" the bare id that firmware before pal care stored.
// 30 fps. A frame redraws and sends only the badge's rows (~2/3 of the panel),
// so the loop keeps time for the melody and the hold fill between frames.
constexpr uint32_t PAL_FRAME_MS = 33;
// The welcome after equipping: the pal slides in, then it waits a beat.
constexpr uint32_t PAL_WELCOME_SLIDE_MS = 500;
constexpr uint32_t PAL_WELCOME_MS = 2200;

// ---- Mood (docs/mood.md) ---------------------------------------------------
//
// The state machine lives in mood.h and knows nothing about hardware; this is
// its clock, its flash and its buzzer. Uptime comes from esp_timer: 64-bit,
// runs through light sleep, never wraps. The wall clock only exists once a
// phone has set it. -DLC_MOOD_FAST makes uptime run 600x — an hour in six
// seconds, the whole week in about seventeen minutes — for bench testing.
constexpr uint32_t BREATH_MS = 5000;  // Sleep face breath period, awake; asleep it is the VCOM tick

// The breath is a liveness cue for whoever just put the camera down, and an
// hour later nobody is watching it. It runs for BREATH_WINDOW_MS after the face
// goes up, restarted by a tap because a tap says someone is there; after that
// the sleep tick stretches and only the mood and a battery sample ride it. The
// panel holds the face either way — on v3q its VCOM is U8's job, not ours — and
// a handful of random daily chirps does not need sub-minute timing.
constexpr uint32_t BREATH_WINDOW_MS = 120000;
constexpr uint32_t SLEEP_TICK_MS = 60000;

// Info asks twice a second while a phone is connected; the ADC does not need
// telling that often, and the mood and the clock are free.
constexpr uint32_t TELEMETRY_SAMPLE_MS = 5000;

// ADC noise is tens of mV once doubled, so the last digit would flap on a
// still battery — and each flap costs a full frame. Widen if it looks jumpy.
constexpr uint32_t BATTERY_DEADBAND_MV = 20;

// Standby readings go to flash, because the one thing that cannot be done to a
// sleeping camera is read its serial port — the port stays enumerated and
// nothing answers. Pull them afterwards with the console's `log`. Five minutes
// is coarse enough that the writes are nothing against a 4mA idle and fine
// enough to show the knee of a discharge curve; unplugging is logged the moment
// it happens, since that is where a standby run starts.
constexpr uint32_t BATTERY_LOG_INTERVAL_MS = 5 * 60 * 1000;

// After a tap in light sleep the chip is up for the sound anyway, so the pal
// idles a few seconds before going back to rest: ~60 mA for 4 s per tap,
// against a pal that otherwise never moves on battery. A press ends it early;
// the level-triggered GPIO wake then picks that press up as usual.
constexpr uint32_t TAP_IDLE_MS = 4000;

// main.cpp
extern Mode mode;
extern bool lastPressed;
extern uint32_t lastActivityTime;
extern bool hintToastShowing;
extern uint32_t captureShownAt;
extern uint8_t* savedFrame;
extern uint32_t pressStartedAt;
extern bool gestureActive;
extern bool holdFired;
extern uint32_t dismissStartedAt;
extern const char* pendingToast;
extern bool captureHeld;
extern int galleryOrdinal;
extern bool galleryDirty;
extern bool galleryToastWasVisible;
extern bool saveToastWasVisible;
extern uint8_t galleryBits[Display::PHOTO_BYTES * Display::PHOTO_HEIGHT];
extern uint8_t offeredPal;
extern uint32_t palShownAt;
extern uint32_t palDrawnAt;
extern bool palToastWasVisible;
extern uint32_t palFrames;
extern uint32_t palFpsSince;
extern bool palFromMenu;
extern uint32_t palWelcomeAt;
extern uint8_t palWelcomeHappiness;
extern bool cameraReady;
extern bool radioOn;
void waitForStableRelease(uint32_t stableMs);
bool confirmPressed();
bool cameraOn();
void cameraOff();
float easeOutCubic(float p);
void startDismiss(const char* toast);
void enterPalFound();
void leavePalFound(const char* toast);
void enterPalWelcome();
void leavePalWelcome();
void showGalleryPhoto();
void enterGallery();
void leaveGallery();
void returnToViewfinder();

// battery.cpp
extern char sleepStatus[16];
extern uint32_t sleepStatusMv;
extern bool sleepStatusChg;
extern bool sleepStatusUsb;
extern bool lastUsb;
extern uint32_t batterySampledAt;
extern uint16_t batterySampleMv;
extern uint8_t prevBatteryBars;
extern uint32_t batteryLoggedAt;
extern bool batteryLogged;
uint32_t batteryMv();
bool onCharge();
bool onUsb();
void readTelemetry(Sync::Telemetry* out);
void logBattery(uint32_t mv, bool chg, bool chgChanged);
bool sampleBattery(bool standby = true);
void serviceUsb(bool asleep);

// mood_glue.cpp
extern Pal::State palState;
extern Mood::State moodState;
extern Chirp::Rng moodRng;
extern int16_t moodTzMin;
extern bool moodTzKnown;
extern uint8_t sleepFaceBand;
extern bool sleepBreathIn;
extern uint32_t sleepFaceDrawnAt;
void palBoot();
void palPersist(const char* why);
bool palShowCare();
Mood::Clock moodClock();
void moodPersist(const char* why);
void moodBoot();
void applyMute();
void moodOnPhoto();
void moodOnClock(bool tzKnown, int16_t tzMin);
void playMoodChirp(uint8_t happiness, const char* why, int count = 0);
void serviceMood(bool quiet, const char* why);
uint8_t moodHappiness();
void showSleepFace(bool full);
void feedPal(const char* why);
void idlePalAfterTap();
void sleepTap(const char* why);
void breatheSleepFace();

// menu.cpp
extern const char* MENU_ITEMS[];
extern int menuSelected;
void showMenu();
void enterMenu();
void leaveMenu();
void menuRunDiagnostics();
void menuShareBlog();
void menuSoundTest();
void menuContrastTest();
void menuFactoryReset();
void menuReboot();
void menuMute();
void menuUnmute();
void menuPalScreen();
void menuSelect();

// sleep.cpp
extern uint32_t asleepSince;
void completeWakeFace(bool fromHold = true);
void enterWakeViewfinder();
uint32_t heldToWake();
void enterAsleep();
void wakeFromAsleep(bool fromHold = false);
void enterSleepMode();

// phone.cpp
extern Mode modeBeforePairing;
extern uint32_t pairingStartedAt;
extern bool helloShowing;
extern uint32_t pairingDoneAt;
extern Mode modeBeforeUpdate;
extern int updatePercent;
extern uint32_t updateEndedAt;
extern bool updateFailed;
void takeOverForPairing();
void enterPairing(uint32_t code);
void leavePairing();
void enterUpdating(uint32_t size);
void leaveUpdating();
void finishUpdating();
void failUpdating(uint32_t status);
void enterPrompt(uint8_t kind);
void handleSyncEvents();

}  // namespace App
