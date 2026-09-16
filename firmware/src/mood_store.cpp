#include "mood_store.h"

#include <Preferences.h>

namespace MoodStore {

static const char* NS = "mood";

bool load(Mood::Persisted* out) {
    Preferences prefs;
    if (!prefs.begin(NS, true)) return false;
    bool ok = false;
    if (prefs.getBytesLength("state") == sizeof(Mood::Persisted)) {
        prefs.getBytes("state", out, sizeof(Mood::Persisted));
        ok = out->version == Mood::PERSISTED_VERSION;
    }
    prefs.end();
    return ok;
}

void save(const Mood::Persisted& p) {
    Preferences prefs;
    if (!prefs.begin(NS, false)) return;
    prefs.putBytes("state", &p, sizeof(p));
    prefs.end();
}

bool loadTz(int16_t* tzMin) {
    Preferences prefs;
    if (!prefs.begin(NS, true)) return false;
    bool ok = prefs.getBytesLength("tz") == sizeof(int16_t);
    if (ok) prefs.getBytes("tz", tzMin, sizeof(int16_t));
    prefs.end();
    return ok;
}

void saveTz(int16_t tzMin) {
    Preferences prefs;
    if (!prefs.begin(NS, false)) return;
    prefs.putBytes("tz", &tzMin, sizeof(tzMin));
    prefs.end();
}

}
