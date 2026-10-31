#pragma once

#include <Arduino.h>

// BLE sync service: the camera as a GATT peripheral the bridge app pulls
// photos from. Protocol in docs/protocol.md §3, byte layout in
// sync_protocol.h, NimBLE plumbing in sync.cpp.
//
// Threading: NimBLE runs its own task. Nothing in its callbacks touches the
// display, the filesystem or Storage's tables — they only stash the write and
// note the time. loop() (main thread) does all the work and raises the
// callbacks below, so main.cpp can draw toasts without a mutex in sight.
//
// Power: the radio runs while the camera is awake and for a while behind the
// sleep face (main.cpp keeps advertising after the idle timeout). end() before
// light sleep tears the whole stack down, begin() after wake rebuilds it
// (~150ms, on a wake that already pays a few hundred for the camera).
// activeRecently() is how main.cpp knows a phone is busy and that window
// should stretch.
namespace Sync {

struct Event {
    enum Kind {
        None, Connected, Disconnected, Passkey, PairingDone, Sent, ClockSet,
        // The phone is waiting for a particular photo (§3.3 PROMPT): the
        // camera says which on its own screen rather than leaving the holder
        // to guess why their phone is showing them a card.
        Prompt,
        // The phone asked for a chirp, `value` times (§3.3).
        Chirp,
        // Firmware update (docs/protocol.md §3.7). These are the only events
        // besides pairing that take over the screen, because an update lasts
        // minutes and must not be interrupted by an idle timeout.
        UpdateBegan, UpdateProgress, UpdateReady, UpdateFailed,
    };
    Kind kind = None;
    uint32_t value = 0;   // Passkey: the 6 digits. PairingDone: 1 ok / 0 failed. Sent: count this connection.
                          // ClockSet: low 16 bits the UTC offset in minutes (as int16), CLOCK_TZ_KNOWN set if the phone sent one.
                          // Prompt: SyncProto::PROMPT_*.
                          // UpdateBegan: image size. UpdateProgress: percent. UpdateFailed: the protocol status.
};

constexpr uint32_t CLOCK_TZ_KNOWN = 0x80000000u;

// What Info reports that only main.cpp can read: it owns the ADC pins and the
// mood. Asked for while a phone is connected, so an idle camera pays nothing.
struct Telemetry {
    uint16_t vbatMv;
    uint8_t batteryPercent;
    bool usb;
    bool charging;
    uint8_t happiness;
    uint32_t moodElapsedS;
};
void setTelemetry(void (*provider)(Telemetry* out));

void begin();   // Init NimBLE, build the service, start advertising. Idempotent.
void end();     // Stop advertising, drop the connection, deinit NimBLE.
void loop();    // Service the pending command, pump the stream, refresh Info.

// Drain UI-relevant events, one per call, from the main thread.
bool nextEvent(Event* out);

bool connected();
bool busy();                    // A photo stream or a firmware update is in progress
bool updating();                // A firmware update session is open

// A phone is connected and did something within the last minute.
bool activeRecently(uint32_t now);
// Drop every phone this camera has let in, so the next one is asked for the passkey again.
// Only the camera's half: the phone keeps its own bond until it is forgotten there too.
bool forgetPhones();

}
