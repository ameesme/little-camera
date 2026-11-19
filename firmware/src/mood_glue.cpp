#include "app.h"

namespace App {

// ---- Mood (docs/mood.md) ---------------------------------------------------
//
// The state machine lives in mood.h and knows nothing about hardware; this is
// its clock, its flash and its buzzer. Uptime comes from esp_timer: 64-bit,
// runs through light sleep, never wraps. The wall clock only exists once a
// phone has set it. -DLC_MOOD_FAST makes uptime run 600x — an hour in six
// seconds, the whole week in about seventeen minutes — for bench testing.

Pal::State palState;

// After moodBoot(): the pal's life runs on the mood's clock.
void palBoot() {
    Pal::Persisted p = {};
    bool have = false;
    uint8_t legacyId = 0;
    Preferences prefs;
    if (prefs.begin("pal", true)) {
        have = prefs.getBytes("care", &p, sizeof p) == sizeof p;
        legacyId = prefs.getUChar("id", 0);
        prefs.end();
    }
    const Mood::Clock c = moodClock();
    Pal::boot(palState, c, have ? &p : nullptr, legacyId);
    Serial.printf("Pal: %u%s food=%u life=%lus\n", palState.id, palState.dead ? " (dead)" : "",
                  palState.food, (unsigned long)Pal::left(palState, c));
}

// On every change, and on the mood's own cadence, so a reset loses at most
// the 15 minutes the mood would.
void palPersist(const char* why) {
    if (!palState.id) return;
    const Pal::Persisted p = Pal::snapshot(palState, moodClock());
    Preferences prefs;
    if (!prefs.begin("pal", false)) return;
    prefs.putBytes("care", &p, sizeof p);
    prefs.putUChar("id", palState.id);
    prefs.end();
    Serial.printf("Pal: saved (%s) food=%u life=%lus%s\n", why, p.food, (unsigned long)p.leftS,
                  p.dead ? " dead" : "");
}

// Hand the sleep face the pal's bar, food and fate. True while the panel shows them out of date.
bool palShowCare() {
    return Display::setPalCare(Pal::life(palState, moodClock()), palState.food, palState.dead);
}

Mood::State moodState;
Chirp::Rng moodRng(1);
int16_t moodTzMin = 0;
bool moodTzKnown = false;
uint8_t sleepFaceBand = 0xFF;   // Band the face was last drawn for
bool sleepBreathIn = false;
uint32_t sleepFaceDrawnAt = 0;

Mood::Clock moodClock() {
    Mood::Clock c;
    uint64_t up = (uint64_t)esp_timer_get_time() / 1000000ULL;
#ifdef LC_MOOD_FAST
    up *= 600;
#endif
    c.uptimeS = (uint32_t)up;
    time_t t = time(nullptr);
    c.epoch = (t > 0 && (uint64_t)t >= Mood::EPOCH_VALID_FROM) ? (uint32_t)t : 0;
    c.tzMin = moodTzMin;
    c.tzKnown = moodTzKnown;
    return c;
}

void moodPersist(const char* why) {
    Mood::Clock c = moodClock();
    MoodStore::save(Mood::snapshot(moodState, c));
    Mood::markPersisted(moodState, c);
    Serial.printf("Mood: saved (%s) elapsed=%lus happiness=%u\n", why,
                  (unsigned long)Mood::elapsed(moodState, c), Mood::happiness(moodState, c));
    if (Pal::alive(palState)) palPersist(why);
}

// After Storage::init(): the newest photo's header is the fallback when NVS
// has no mood yet (first boot of this firmware on a camera with photos).
void moodBoot() {
    moodRng = Chirp::Rng(esp_random());
    moodTzKnown = MoodStore::loadTz(&moodTzMin);
    Mood::Persisted p;
    const bool have = MoodStore::load(&p);
    uint32_t newestEpoch = 0;
    Storage::PhotoInfo info;
    const int newest = Storage::newestIndex();
    if (newest > 0 && Storage::photoInfo(newest, &info)) newestEpoch = info.epoch;
    Mood::Clock c = moodClock();
    Mood::boot(moodState, c, have ? &p : nullptr, newestEpoch, moodRng);
    Serial.printf("Mood: %s elapsed=%lus happiness=%u band=%u clock=%s tz=%s%d\n", have ? "resumed" : "fresh",
                  (unsigned long)Mood::elapsed(moodState, c), Mood::happiness(moodState, c),
                  Chirp::band(Mood::happiness(moodState, c)), c.epoch ? "set" : "unset",
                  moodTzKnown ? "" : "unknown ", moodTzMin);
    if (!have) moodPersist("first boot");
}

// Mute holds until midnight on the chirp schedule's day: local once the phone has set the clock, else a powered-on 24 h period.
void applyMute() {
    const uint32_t now = Mood::detail::dailyNow(moodState, moodClock());
    uint32_t day;
    if (!MoodStore::loadMuteDay(&day) || day != now / Mood::DAY_S) return;
    const uint32_t leftS = Mood::DAY_S - now % Mood::DAY_S;
    Audio::muteUntil(esp_timer_get_time() + (int64_t)leftS * 1000000);
    Serial.printf("Audio: muted for %lus\n", (unsigned long)leftS);
}

void moodOnPhoto() {
    Mood::onPhoto(moodState, moodClock(), moodRng);
    moodPersist("photo");
}

// The phone set the clock (and maybe told us the timezone).
void moodOnClock(bool tzKnown, int16_t tzMin) {
    if (tzKnown && (!moodTzKnown || moodTzMin != tzMin)) {
        moodTzMin = tzMin;
        moodTzKnown = true;
        MoodStore::saveTz(tzMin);
    }
    Mood::onClockSet(moodState, moodClock());
    Pal::onClockSet(palState, moodClock());
    moodPersist("clock");
}

// Compose, log, then play. The play blocks for up to 300ms, so the line is on
// the monitor before the sound. Audio::init first: this is also called straight
// out of light sleep, where the pin has to be claimed again.
void playMoodChirp(uint8_t happiness, const char* why, int count) {
    Audio::init(PIN_BUZZ);
    if (count <= 0) count = 1 + (int)(esp_random() % 3);
    for (int i = 0; i < count; i++) {
        if (i) delay(CHIRP_GAP_MIN_MS + esp_random() % (CHIRP_GAP_MAX_MS - CHIRP_GAP_MIN_MS + 1));
        Chirp::Rng rng(esp_random());
        Chirp::Score s = Chirp::compose(happiness, rng);
        char desc[64];
        Chirp::describe(s, desc, sizeof(desc));
        Serial.printf("%s: chirp %d/%d happiness=%u/255 band=%u %s = %lums\n", why, i + 1, count,
                      happiness, Chirp::band(happiness), desc, (unsigned long)Chirp::totalMs(s));
        Audio::playChirp(s);
    }
}

// Ask the mood what it owes and pay it if this is a good moment. The state
// machine never knows what screen is up; `quiet` is decided by the caller:
// a screen a chirp may interrupt, button up, nothing else sounding, no
// transfer running. A chirp that is not paid stays due for the next call.
void serviceMood(bool quiet, const char* why) {
    Mood::Clock c = moodClock();
    // Silently: a pal can run out at three in the morning. The tombstone says it.
    if (Pal::poll(palState, c)) {
        Serial.printf("Pal: %u died\n", palState.id);
        palPersist("died");
        palShowCare();
    }
    Mood::Due d = Mood::poll(moodState, c, moodRng);
    if (d != Mood::Due::None && quiet) {
        playMoodChirp(moodHappiness(), why);
        Mood::onChirpPlayed(moodState, c, d, moodRng);
        moodPersist(d == Mood::Due::HourChirp ? "hour chirp" : "attention chirp");
    } else if (Mood::persistDue(moodState, c)) {
        moodPersist("cadence");
    }
}

uint8_t moodHappiness() {
    // Share the telemetry sample instead of running the ADC on every viewfinder frame.
    if (!batterySampleMv || millis() - batterySampledAt >= TELEMETRY_SAMPLE_MS ||
        onUsb() != sleepStatusUsb || onCharge() != sleepStatusChg) sampleBattery(false);
    uint8_t h = Mood::happiness(moodState, moodClock());
    const uint8_t ceiling = prevBatteryBars <= 1 ? 63 : (prevBatteryBars == 2 ? 127 : 255);
    if (h > ceiling) h = ceiling;
    Display::setFaceBand(Chirp::band(h));
    return h;
}

// The sleep face for the current mood. A full frame when it first goes up or
// the band moved; a breath (the other frame, face rows only) otherwise.
void showSleepFace(bool full) {
    const uint8_t h = moodHappiness();
    const uint8_t band = Chirp::band(h);
    if (band != sleepFaceBand) full = true;
    sleepFaceBand = band;
    // The viewfinder's sidebar wears the same face, so it learns the band here
    // rather than asking mood.h from inside the panel driver.
    Display::setFaceBand(band);
    // The pal's corner sits outside the face rows a breath sends.
    if (palShowCare()) full = true;

    Display::drawSleep(h, sleepBreathIn, !full);
    sleepFaceDrawnAt = millis();
}

// A tap on the sleep face with food in hand feeds the pal: the item hops over,
// the pal eats with a sound, a heart rises and the bar fills. Blocking for
// ~1.5 s, like the chirp it replaces, because it also runs straight out of
// light sleep where there is no loop to animate from.
void feedPal(const char* why) {
    const Mood::Clock c = moodClock();
    const uint8_t from = Pal::life(palState, c);
    if (!Pal::feed(palState, c)) return;
    Serial.printf("%s: fed pal %u, life %u -> %u/255, %u food left\n", why, palState.id, from,
                  Pal::life(palState, c), palState.food);
    palPersist("fed");
    palShowCare();
    Audio::init(PIN_BUZZ);
    const uint8_t h = moodHappiness();
    Display::drawPalFeed(h, sleepBreathIn, 0, from, true);
    uint32_t start = millis();
    bool chomped = false;
    for (;;) {
        const uint32_t t = millis() - start;
        if (t >= Display::PAL_FEED_MS) break;
        if (!chomped && t >= Display::PAL_FEED_TOSS_MS) {
            chomped = true;
            Display::drawPalFeed(h, sleepBreathIn, Display::PAL_FEED_TOSS_MS, from, false);
            const uint32_t before = millis();
            Audio::playFeed();
            // The animation waits out the blocking sound instead of skipping ahead.
            start += millis() - before;
            continue;
        }
        Display::drawPalFeed(h, sleepBreathIn, t, from, false);
        delay(PAL_FRAME_MS);
    }
    showSleepFace(true);
}
void idlePalAfterTap() {
    const uint32_t start = millis();
    while (millis() - start < TAP_IDLE_MS && !shutterPressed()) {
        Display::animateSleepPal(millis());
        delay(20);
    }
    Display::animateSleepPal(0);
}

// What a tap on the sleep face does: feed the pal if there is food for it, chirp otherwise.
void sleepTap(const char* why) {
    if (Pal::alive(palState) && palState.food) feedPal(why);
    else playMoodChirp(moodHappiness(), why);
}

void breatheSleepFace() {
    sleepBreathIn = !sleepBreathIn;
    showSleepFace(sampleBattery());
}

}  // namespace App
