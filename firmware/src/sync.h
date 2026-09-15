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
    enum Kind { None, Connected, Disconnected, Passkey, PairingDone, Sent, ClockSet };
    Kind kind = None;
    uint32_t value = 0;   // Passkey: the 6 digits. PairingDone: 1 ok / 0 failed. Sent: count this connection.
                          // ClockSet: low 16 bits the UTC offset in minutes (as int16), CLOCK_TZ_KNOWN set if the phone sent one.
};

constexpr uint32_t CLOCK_TZ_KNOWN = 0x80000000u;

void begin();   // Init NimBLE, build the service, start advertising. Idempotent.
void end();     // Stop advertising, drop the connection, deinit NimBLE.
void loop();    // Service the pending command, pump the stream, refresh Info.

// Drain UI-relevant events, one per call, from the main thread.
bool nextEvent(Event* out);

bool connected();
bool busy();                    // A stream is in progress

// A phone is connected and did something within the last minute.
bool activeRecently(uint32_t now);

}
