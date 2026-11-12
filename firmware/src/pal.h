#pragma once

// The companion pal's care: how long it has left, the food it is owed, and
// whether it is still alive. Pure state machine over the mood's clock, like
// mood.h, so tools/hosttest can live a pal's whole life in milliseconds;
// main.cpp owns NVS, the buzzer and the screen.
//
// Life counts down the way the mood counts up: with uptime within a boot,
// resumed from NVS across a reset, and by the wall clock once the phone has
// set it, so powered-off time counts too. Without a wall clock it does not.
//
// Arduino-free, integer-only, C++11.

#include <stdint.h>

#include "chirp.h"
#include "mood.h"

namespace Pal {

constexpr uint8_t COUNT = 4;
// A new pal gets a week, which is also a full bar: feeding never takes it past one.
constexpr uint32_t LIFE_S = Mood::WEEK_S;
// Two days per item at one save in three is about a photo and a half a day to
// keep a pal going: a handful a day keeps it fat, a quiet week starves it.
constexpr uint32_t FEED_S = 2 * Mood::DAY_S;
constexpr uint8_t FOOD_CHANCE_PCT = 33;
constexpr uint8_t FOOD_MAX = 99;  // Two digits on the sleep face

struct Persisted {
    uint8_t version;
    uint8_t id;
    uint8_t food;
    uint8_t dead;
    uint32_t leftS;     // Life left when this was written
    uint32_t dueEpoch;  // When it runs out by the wall clock, 0 if the clock was never set
};
constexpr uint8_t PERSISTED_VERSION = 1;

struct State {
    uint8_t id;  // 0 = none, 1-COUNT
    uint8_t food;
    bool dead;
    uint32_t leftS;
    uint32_t leftAtUptimeS;
    uint32_t dueEpoch;
};

inline bool alive(const State& s) { return s.id && !s.dead; }

inline uint32_t left(const State& s, const Mood::Clock& c) {
    if (!alive(s)) return 0;
    const uint32_t ran = c.uptimeS - s.leftAtUptimeS;
    uint32_t l = ran >= s.leftS ? 0 : s.leftS - ran;
    // Whichever clock says more time went by wins, as in Mood::elapsed().
    if (s.dueEpoch && Mood::clockValid(c)) {
        const uint32_t byWall = s.dueEpoch > c.epoch ? s.dueEpoch - c.epoch : 0;
        if (byWall < l) l = byWall;
    }
    return l;
}

// 0-255 of a full bar. Rounded up, so a pal with any life left shows some.
inline uint8_t life(const State& s, const Mood::Clock& c) {
    const uint32_t l = left(s, c);
    if (l >= LIFE_S) return 255;
    return (uint8_t)(((uint64_t)l * 255 + LIFE_S - 1) / LIFE_S);
}

namespace detail {
inline void setLeft(State& s, const Mood::Clock& c, uint32_t l) {
    s.leftS = l;
    s.leftAtUptimeS = c.uptimeS;
    s.dueEpoch = Mood::clockValid(c) ? c.epoch + l : 0;
}
}  // namespace detail

// A new pal, replacing whatever was there (a tombstone included): a full week, no food.
inline void equip(State& s, const Mood::Clock& c, uint8_t id) {
    s = State();
    s.id = id;
    detail::setLeft(s, c, LIFE_S);
}

// A photo was saved: sometimes it turns up something to eat. True if it did.
inline bool onPhoto(State& s, Chirp::Rng& rng) {
    if (!alive(s) || s.food >= FOOD_MAX || !rng.chance(FOOD_CHANCE_PCT)) return false;
    s.food++;
    return true;
}

// One item eaten: FEED_S more, never past a full bar. False with nothing to give.
inline bool feed(State& s, const Mood::Clock& c) {
    if (!alive(s) || !s.food) return false;
    uint32_t l = left(s, c) + FEED_S;
    if (l > LIFE_S) l = LIFE_S;
    s.food--;
    detail::setLeft(s, c, l);
    return true;
}

// True on the call that finds it has run out. Its food goes with it: the
// food was its kind's, and the next pal may be another kind.
inline bool poll(State& s, const Mood::Clock& c) {
    if (!alive(s) || left(s, c)) return false;
    s.dead = true;
    s.food = 0;
    return true;
}

// The phone set the wall clock: pin the end to it, so powered-off time counts from here on.
inline void onClockSet(State& s, const Mood::Clock& c) {
    if (!alive(s) || !Mood::clockValid(c)) return;
    detail::setLeft(s, c, left(s, c));
}

inline Persisted snapshot(const State& s, const Mood::Clock& c) {
    Persisted p = {};
    p.version = PERSISTED_VERSION;
    p.id = s.id;
    p.food = s.food;
    p.dead = s.dead;
    p.leftS = left(s, c);
    p.dueEpoch = s.dueEpoch;
    return p;
}

// `legacyId` is the bare pal id firmware before pal care stored: that pal
// starts its first week now rather than being found already starved.
inline void boot(State& s, const Mood::Clock& c, const Persisted* p, uint8_t legacyId) {
    s = State();
    if (p && p->version == PERSISTED_VERSION && p->id >= 1 && p->id <= COUNT) {
        s.id = p->id;
        s.food = p->food > FOOD_MAX ? FOOD_MAX : p->food;
        s.dead = p->dead != 0;
        s.leftS = p->leftS > LIFE_S ? LIFE_S : p->leftS;
        s.leftAtUptimeS = c.uptimeS;
        s.dueEpoch = p->dueEpoch;
        return;
    }
    if (legacyId >= 1 && legacyId <= COUNT) equip(s, c, legacyId);
}

}  // namespace Pal
