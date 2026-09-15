#pragma once

#include <Arduino.h>

#include "mood.h"

// The mood's NVS home: one versioned blob under Preferences("mood"), plus the
// timezone the phone sent. Writes happen on a photo, on a chirp, on a clock
// change and at most every 15 minutes otherwise (Mood::persistDue), which is
// on the order of a hundred writes a day at worst — nothing for NVS.
namespace MoodStore {

bool load(Mood::Persisted* out);            // false if absent or a different version
void save(const Mood::Persisted& p);
bool loadTz(int16_t* tzMin);                // false if the phone never sent one
void saveTz(int16_t tzMin);

}
