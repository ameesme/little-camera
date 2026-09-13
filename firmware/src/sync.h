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
// Power: the radio only runs while the camera is awake. end() before light
// sleep tears the whole stack down, begin() after wake rebuilds it (~150ms,
// on a wake that already pays a few hundred for the camera). keepAwake() is
// the policy that stretches the idle timeout while a phone is busy.
namespace Sync {

struct Event {
    enum Kind { None, Connected, Disconnected, Passkey, PairingDone, Sent };
    Kind kind = None;
    uint32_t value = 0;   // Passkey: the 6 digits. PairingDone: 1 ok / 0 failed. Sent: count this connection.
};

void begin();   // Init NimBLE, build the service, start advertising. Idempotent.
void end();     // Stop advertising, drop the connection, deinit NimBLE.
void loop();    // Service the pending command, pump the stream, refresh Info.

// Drain UI-relevant events, one per call, from the main thread.
bool nextEvent(Event* out);

bool connected();
bool busy();                    // A stream is in progress

// How long to keep the device awake beyond the normal idle timeout.
// `idleForMs` is how long since the last local activity (button, console).
bool keepAwake(uint32_t now, uint32_t idleForMs);

}
