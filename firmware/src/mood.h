#pragma once

// The camera's mood: how long since the last photo, and what it owes because
// of it. Behaviour in docs/mood.md. Pure state machine over a caller-supplied
// clock so tools/hosttest can play a month through it in milliseconds;
// main.cpp owns the real clock, the NVS blob and the buzzer.
//
// Time lives on the "mood clock": seconds since the last photo. Within a boot
// it advances with uptime. Across a reset it resumes from the persisted
// value. Once the phone has set the wall clock and the last photo's epoch is
// known, the epoch difference wins, so powered-off time counts too. Without
// a wall clock, powered-off time is simply not known and does not count.
//
// Arduino-free, integer-only, C++11, like chirp.h.

#include <stdint.h>

#include "chirp.h"

namespace Mood {

constexpr uint32_t WEEK_S = 7UL * 24 * 3600;
constexpr uint32_t HOUR_CHIRP_MIN_S = 55 * 60;
constexpr uint32_t HOUR_CHIRP_MAX_S = 65 * 60;
constexpr uint32_t HOUR_CHIRP_GRACE_S = 3600;  // Slept through the night: forget it
constexpr uint32_t ATTENTION_AFTER_S = 24UL * 3600;
constexpr uint32_t FIRST_INTERVAL_MAX_S = 8UL * 3600;   // First attention chirp: 0-8h past the day mark
constexpr uint32_t INTERVAL_MIN_S = 12UL * 3600;        // Then 12-20h apart: at most two in any 24h
constexpr uint32_t INTERVAL_MAX_S = 20UL * 3600;
constexpr uint8_t NIGHT_FROM_H = 22;
constexpr uint8_t NIGHT_TO_H = 8;
constexpr uint32_t MORNING_SPREAD_S = 2 * 3600;         // Deferred chirps land 08:00 + 0-2h
constexpr uint32_t PERSIST_EVERY_S = 15 * 60;
constexpr uint32_t EPOCH_VALID_FROM = 1600000000UL;     // Anything smaller is "clock never set"

struct Clock {
    uint32_t uptimeS;  // Monotonic, runs through light sleep, never wraps (64-bit source)
    uint32_t epoch;    // Unix seconds, 0 if the wall clock was never set
    int16_t tzMin;     // Minutes east of UTC
    bool tzKnown;
};

// What goes to flash. Versioned so a future layout can migrate or ignore it.
struct Persisted {
    uint8_t version;
    uint32_t lastPhotoEpoch;
    uint32_t elapsedS;
    uint32_t hourChirpAtS;
    uint32_t nextChirpAtS;
};
constexpr uint8_t PERSISTED_VERSION = 1;

struct State {
    uint32_t lastPhotoEpoch;      // 0 if unknown when taken; back-filled by onClockSet()
    uint32_t elapsedS;            // Mood-clock seconds at elapsedAtUptimeS
    uint32_t elapsedAtUptimeS;
    uint32_t hourChirpAtS;        // Mood-clock time of the one-hour chirp; 0 = none owed
    uint32_t nextChirpAtS;        // Mood-clock time of the next attention chirp; 0 = not drawn yet
    uint32_t lastPersistUptimeS;
};

enum class Due : uint8_t { None, HourChirp, Attention };

inline bool clockValid(const Clock& c) { return c.epoch >= EPOCH_VALID_FROM; }

inline uint32_t elapsed(const State& s, const Clock& c) {
    uint32_t e = s.elapsedS + (c.uptimeS - s.elapsedAtUptimeS);
    if (s.lastPhotoEpoch && clockValid(c) && c.epoch > s.lastPhotoEpoch) {
        uint32_t byWall = c.epoch - s.lastPhotoEpoch;
        if (byWall > e) e = byWall;
    }
    return e;
}

// 255 the moment a photo is taken, 0 a week later, linear in between.
inline uint8_t happiness(const State& s, const Clock& c) {
    uint32_t e = elapsed(s, c);
    if (e >= WEEK_S) return 0;
    return (uint8_t)(255 - (uint32_t)(((uint64_t)e * 255) / WEEK_S));
}

inline uint8_t happinessFor(uint32_t elapsedS) {
    if (elapsedS >= WEEK_S) return 0;
    return (uint8_t)(255 - (uint32_t)(((uint64_t)elapsedS * 255) / WEEK_S));
}

namespace detail {

inline uint32_t localSecondOfDay(const Clock& c) {
    int64_t local = (int64_t)c.epoch + (int64_t)c.tzMin * 60;
    int64_t sod = local % 86400;
    if (sod < 0) sod += 86400;
    return (uint32_t)sod;
}

inline bool isNight(const Clock& c) {
    if (!clockValid(c) || !c.tzKnown) return false;  // No clock, no night: decided in docs/mood.md
    uint32_t h = localSecondOfDay(c) / 3600;
    return h >= NIGHT_FROM_H || h < NIGHT_TO_H;
}

// Seconds from now until 08:00 local plus a random 0-2h.
inline uint32_t untilMorning(const Clock& c, Chirp::Rng& rng) {
    uint32_t sod = localSecondOfDay(c);
    uint32_t target = (uint32_t)NIGHT_TO_H * 3600 + rng.below(MORNING_SPREAD_S + 1);
    return sod < target ? target - sod : 86400 - sod + target;
}

}  // namespace detail

// A photo was just saved: happy again, and the one-hour chirp is owed.
inline void onPhoto(State& s, const Clock& c, Chirp::Rng& rng) {
    s.lastPhotoEpoch = clockValid(c) ? c.epoch : 0;
    s.elapsedS = 0;
    s.elapsedAtUptimeS = c.uptimeS;
    s.hourChirpAtS = rng.between(HOUR_CHIRP_MIN_S / 60, HOUR_CHIRP_MAX_S / 60) * 60;
    s.nextChirpAtS = 0;
}

// Boot: resume the persisted blob if there is one, else start from what the
// flash says. A fresh device (no state, no photos) is a happy one that just
// took a picture: it gets its one-hour chirp too.
inline void boot(State& s, const Clock& c, const Persisted* p, uint32_t newestPhotoEpoch, Chirp::Rng& rng) {
    s.elapsedAtUptimeS = c.uptimeS;
    s.lastPersistUptimeS = c.uptimeS;
    if (p && p->version == PERSISTED_VERSION) {
        s.lastPhotoEpoch = p->lastPhotoEpoch;
        s.elapsedS = p->elapsedS;
        s.hourChirpAtS = p->hourChirpAtS;
        s.nextChirpAtS = p->nextChirpAtS;
        return;
    }
    s.lastPhotoEpoch = newestPhotoEpoch;
    s.elapsedS = (newestPhotoEpoch && clockValid(c) && c.epoch > newestPhotoEpoch) ? c.epoch - newestPhotoEpoch : 0;
    s.hourChirpAtS = s.elapsedS < HOUR_CHIRP_MIN_S ? rng.between(HOUR_CHIRP_MIN_S / 60, HOUR_CHIRP_MAX_S / 60) * 60 : 0;
    s.nextChirpAtS = 0;
}

// The phone set the wall clock. If the last photo predates any clock, pin it
// now so powered-off time counts from here on.
inline void onClockSet(State& s, const Clock& c) {
    if (s.lastPhotoEpoch || !clockValid(c)) return;
    uint32_t e = elapsed(s, c);
    s.lastPhotoEpoch = c.epoch > e ? c.epoch - e : EPOCH_VALID_FROM;
}

// What is owed right now. Also does the bookkeeping that has to happen at
// poll time: drawing the first attention interval once the day mark passes,
// deferring a night-time chirp to the morning, forgetting a one-hour chirp
// that the night swallowed. Call from a place where a chirp may play; the
// caller plays it and then calls onChirpPlayed().
inline Due poll(State& s, const Clock& c, Chirp::Rng& rng) {
    const uint32_t e = elapsed(s, c);
    const bool night = detail::isNight(c);

    if (s.hourChirpAtS && e >= s.hourChirpAtS) {
        if (!night) return Due::HourChirp;
        if (e >= s.hourChirpAtS + HOUR_CHIRP_GRACE_S) s.hourChirpAtS = 0;
    }

    if (e >= ATTENTION_AFTER_S) {
        if (s.nextChirpAtS == 0) {
            s.nextChirpAtS = e + rng.below(FIRST_INTERVAL_MAX_S + 1);
            return Due::None;
        }
        if (e >= s.nextChirpAtS) {
            if (night) {
                s.nextChirpAtS = e + detail::untilMorning(c, rng);
                return Due::None;
            }
            return Due::Attention;
        }
    }
    return Due::None;
}

inline void onChirpPlayed(State& s, const Clock& c, Due what, Chirp::Rng& rng) {
    const uint32_t e = elapsed(s, c);
    if (what == Due::HourChirp) s.hourChirpAtS = 0;
    if (what == Due::Attention) s.nextChirpAtS = e + INTERVAL_MIN_S + rng.below(INTERVAL_MAX_S - INTERVAL_MIN_S + 1);
}

inline bool persistDue(const State& s, const Clock& c) { return c.uptimeS - s.lastPersistUptimeS >= PERSIST_EVERY_S; }

inline Persisted snapshot(const State& s, const Clock& c) {
    Persisted p;
    p.version = PERSISTED_VERSION;
    p.lastPhotoEpoch = s.lastPhotoEpoch;
    p.elapsedS = elapsed(s, c);
    p.hourChirpAtS = s.hourChirpAtS;
    p.nextChirpAtS = s.nextChirpAtS;
    return p;
}

inline void markPersisted(State& s, const Clock& c) { s.lastPersistUptimeS = c.uptimeS; }

}  // namespace Mood
