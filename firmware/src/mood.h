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
constexpr uint32_t DAY_S = 24UL * 3600;
constexpr uint8_t NIGHT_FROM_H = 22;
constexpr uint8_t NIGHT_TO_H = 8;
constexpr uint8_t MAX_DAILY_CHIRPS = 4;
constexpr uint32_t PERSIST_EVERY_S = 15 * 60;
constexpr uint32_t EPOCH_VALID_FROM = 1600000000UL;     // Anything smaller is "clock never set"

struct Clock {
    uint32_t uptimeS;  // Monotonic, runs through light sleep, never wraps (64-bit source)
    uint32_t epoch;    // Unix seconds, 0 if the wall clock was never set
    int16_t tzMin;     // Minutes east of UTC
    bool tzKnown;
};

// The separate schedule clock keeps photos from resetting the day's appointments.
struct Daily {
    uint32_t nowS;
    uint32_t day;
    uint32_t atS[MAX_DAILY_CHIRPS];
    uint8_t count;
    uint8_t next;
    bool local;
};

struct LegacyPersisted {
    uint8_t version;
    uint32_t lastPhotoEpoch;
    uint32_t elapsedS;
    uint32_t hourChirpAtS;
    uint32_t nextChirpAtS;
};

struct Persisted {
    uint8_t version;
    uint32_t lastPhotoEpoch;
    uint32_t elapsedS;
    uint32_t hourChirpAtS;
    Daily daily;
};
constexpr uint8_t PERSISTED_VERSION = 2;

// Keep the mood and pending hour chirp when upgrading the old interval scheduler.
inline Persisted migrate(const LegacyPersisted& old) {
    Persisted p = {};
    p.version = PERSISTED_VERSION;
    p.lastPhotoEpoch = old.lastPhotoEpoch;
    p.elapsedS = old.elapsedS;
    p.hourChirpAtS = old.hourChirpAtS;
    return p;
}

struct State {
    uint32_t lastPhotoEpoch;
    uint32_t elapsedS;
    uint32_t elapsedAtUptimeS;
    uint32_t hourChirpAtS;
    uint32_t lastPersistUptimeS;
    Daily daily;
    uint32_t dailyAtUptimeS;
    bool scheduleDirty;
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

inline uint32_t dailyNow(const State& s, const Clock& c) {
    if (clockValid(c) && c.tzKnown) return (uint32_t)((int64_t)c.epoch + (int64_t)c.tzMin * 60);
    return s.daily.nowS + (c.uptimeS - s.dailyAtUptimeS);
}

inline void scheduleDay(State& s, const Clock& c, Chirp::Rng& rng) {
    uint32_t now = dailyNow(s, c);
    bool local = s.daily.local || (clockValid(c) && c.tzKnown);
    if (s.daily.count && s.daily.day == now / DAY_S && s.daily.local == local) return;
    const bool skipPast = !s.daily.count || s.daily.local != local || now / DAY_S != s.daily.day + 1;
    s.daily.day = now / DAY_S;
    s.daily.local = local;
    s.daily.count = 3 + rng.below(2);
    s.daily.next = 0;
    const uint32_t fromMin = local ? NIGHT_TO_H * 60 : 0;
    const uint32_t untilMin = local ? NIGHT_FROM_H * 60 : 24 * 60;
    for (uint8_t i = 0; i < s.daily.count; ++i) {
        uint32_t at;
        bool duplicate;
        do {
            // Leave the final minute free so a minute-long sleep tick can still pay it today.
            at = (fromMin + rng.below(untilMin - fromMin - 1)) * 60;
            duplicate = false;
            for (uint8_t j = 0; j < i; ++j) if (s.daily.atS[j] == at) duplicate = true;
        } while (duplicate);
        uint8_t j = i;
        while (j && s.daily.atS[j - 1] > at) {
            s.daily.atS[j] = s.daily.atS[j - 1];
            --j;
        }
        s.daily.atS[j] = at;
    }
    // Booting late or returning after days away should not replay missed appointments.
    if (skipPast) {
        while (s.daily.next < s.daily.count && s.daily.atS[s.daily.next] < now % DAY_S) ++s.daily.next;
    }
    s.scheduleDirty = true;
}

}  // namespace detail

// A photo was just saved: happy again, and the one-hour chirp is owed.
inline void onPhoto(State& s, const Clock& c, Chirp::Rng& rng) {
    s.lastPhotoEpoch = clockValid(c) ? c.epoch : 0;
    s.elapsedS = 0;
    s.elapsedAtUptimeS = c.uptimeS;
    s.hourChirpAtS = rng.between(HOUR_CHIRP_MIN_S / 60, HOUR_CHIRP_MAX_S / 60) * 60;
}

// Boot: resume the persisted blob if there is one, else start from what the
// flash says. A fresh device (no state, no photos) is a happy one that just
// took a picture: it gets its one-hour chirp too.
inline void boot(State& s, const Clock& c, const Persisted* p, uint32_t newestPhotoEpoch, Chirp::Rng& rng) {
    s.daily = {};
    s.dailyAtUptimeS = c.uptimeS;
    s.scheduleDirty = false;
    s.elapsedAtUptimeS = c.uptimeS;
    s.lastPersistUptimeS = c.uptimeS;
    if (p && p->version == PERSISTED_VERSION) {
        s.lastPhotoEpoch = p->lastPhotoEpoch;
        s.elapsedS = p->elapsedS;
        s.hourChirpAtS = p->hourChirpAtS;
        s.daily = p->daily;
        return;
    }
    s.lastPhotoEpoch = newestPhotoEpoch;
    s.elapsedS = (newestPhotoEpoch && clockValid(c) && c.epoch > newestPhotoEpoch) ? c.epoch - newestPhotoEpoch : 0;
    s.hourChirpAtS = s.elapsedS < HOUR_CHIRP_MIN_S ? rng.between(HOUR_CHIRP_MIN_S / 60, HOUR_CHIRP_MAX_S / 60) * 60 : 0;
}

// The phone set the wall clock. If the last photo predates any clock, pin it
// now so powered-off time counts from here on.
inline void onClockSet(State& s, const Clock& c) {
    if (s.lastPhotoEpoch || !clockValid(c)) return;
    uint32_t e = elapsed(s, c);
    s.lastPhotoEpoch = c.epoch > e ? c.epoch - e : EPOCH_VALID_FROM;
}

// The caller plays a due chirp on a quiet screen, then acknowledges it below.
inline Due poll(State& s, const Clock& c, Chirp::Rng& rng) {
    detail::scheduleDay(s, c, rng);
    const uint32_t e = elapsed(s, c);
    const uint32_t sod = detail::dailyNow(s, c) % DAY_S;
    const bool night = s.daily.local && (sod < NIGHT_TO_H * 3600UL || sod >= NIGHT_FROM_H * 3600UL);

    if (s.hourChirpAtS && e >= s.hourChirpAtS) {
        if (!night) return Due::HourChirp;
        if (e >= s.hourChirpAtS + HOUR_CHIRP_GRACE_S) s.hourChirpAtS = 0;
    }

    if (s.daily.next < s.daily.count && sod >= s.daily.atS[s.daily.next] && !night)
        return Due::Attention;
    return Due::None;
}

inline void onChirpPlayed(State& s, const Clock& c, Due what, Chirp::Rng&) {
    if (what == Due::HourChirp) s.hourChirpAtS = 0;
    if (what == Due::Attention) {
        // Several appointments missed on a busy screen earn one sound, not a backlog.
        uint32_t sod = detail::dailyNow(s, c) % DAY_S;
        while (s.daily.next < s.daily.count && s.daily.atS[s.daily.next] <= sod) ++s.daily.next;
    }
}

inline bool persistDue(const State& s, const Clock& c) {
    return s.scheduleDirty || c.uptimeS - s.lastPersistUptimeS >= PERSIST_EVERY_S;
}

inline Persisted snapshot(const State& s, const Clock& c) {
    Persisted p = {};
    p.version = PERSISTED_VERSION;
    p.lastPhotoEpoch = s.lastPhotoEpoch;
    p.elapsedS = elapsed(s, c);
    p.hourChirpAtS = s.hourChirpAtS;
    p.daily = s.daily;
    p.daily.nowS = detail::dailyNow(s, c);
    return p;
}

inline void markPersisted(State& s, const Clock& c) {
    s.lastPersistUptimeS = c.uptimeS;
    s.scheduleDirty = false;
}

}  // namespace Mood
