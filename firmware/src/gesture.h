#pragma once

#include <stdint.h>

// What one button says on a screen with a hold action: a press that is still
// down at the threshold is a hold, one released before it is a tap. Pure and
// Arduino-free so the host tests can walk it; the caller reads the pin, runs the
// debounce and passes the clock in.
//
// The hold fires *on* the threshold, not on release, and the time held is a
// signed difference: a clock sampled before the press was confirmed reads as
// zero held rather than wrapping to 49 days and firing at once. That bug shipped
// once (trash on every press), which is why it is pinned by a test now.
namespace Gesture {

struct State {
    uint32_t startedAt = 0;  // When the current press began
    bool active = false;     // A press this screen saw begin is in progress
    bool fired = false;      // It already crossed the hold threshold
};

enum class Event : uint8_t { None, Began, Hold, Tap };

inline uint32_t held(const State& s, uint32_t nowMs) {
    const int32_t d = (int32_t)(nowMs - s.startedAt);
    return d > 0 ? (uint32_t)d : 0;
}

// `confirmed` is the debounce's verdict on a press edge and is only looked at
// on one, so the caller can leave it true where it does not debounce.
inline Event step(State& s, bool pressed, bool wasPressed, bool confirmed, uint32_t nowMs, uint32_t holdMs) {
    Event e = Event::None;
    if (pressed && !wasPressed && confirmed) {
        s.startedAt = nowMs;
        s.active = true;
        s.fired = false;
        e = Event::Began;
    } else if (pressed && s.active && !s.fired && held(s, nowMs) >= holdMs) {
        s.fired = true;
        e = Event::Hold;
    } else if (!pressed && wasPressed && s.active && !s.fired) {
        e = Event::Tap;
    }
    if (!pressed) s.active = false;
    return e;
}

}  // namespace Gesture
