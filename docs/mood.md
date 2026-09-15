# Mood

The camera wants to be used. Left alone it gets a little anxious over the course of a week and asks for attention, the way a tamagotchi would, but quieter. Everything below keys off one number: **how long ago the last photo was taken.**

Status: built. The sound engine (`firmware/src/chirp.h`, `Audio::playChirp`), the mood clock and scheduler (`firmware/src/mood.h`, glue in `main.cpp`, NVS in `mood_store.cpp`), the face (`Display::drawSleep`) and the tap response. Host-tested and previewed; the first real week on hardware is still ahead.

## Happiness

`happiness` is 1.0 the moment a photo is saved and falls linearly to 0.0 over 7 days without one. The firmware carries it as `uint8_t` 0–255.

| Since last photo | Happiness | Face | Sound |
|---|---|---|---|
| 0 – 1 h | 1.0 – 0.99 | asleep, smiling | none |
| ~1 h | | | **one** chirp, happy |
| 1 h – 24 h | 0.99 – 0.86 | smiling | none |
| 1 – 3 d | 0.86 – 0.57 | smile flattening | chirps, harmonic with the odd slide |
| 3 – 5 d | 0.57 – 0.29 | flat | chirps go off-scale, a sweep or a short hiss creeps in |
| 5 – 7 d | 0.29 – 0.0 | mouth turned down | dissonant, noisy, sweeps in both directions |
| > 7 d | 0.0 | as sad as it gets | same, no further escalation |

Taking a photo resets everything at once: face back to the smile, no chirps for an hour.

### What "time since last photo" can and cannot know

- Within one boot, exactly: `millis()` runs through light sleep, and every photo header carries `up=`.
- Across a **reset**, still fine: the system clock survives a software reset, and the last photo's uptime and boot number are on disk.
- Across a **power loss** (dead battery, cable pulled), unknown. The camera has no clock battery. Rule: mood state is persisted in NVS (`Preferences("mood")`, a 17-byte blob: the last photo's unix time if the clock was known, the seconds elapsed since it, and the two chirp appointments) and resumed on boot. Time spent powered off does not count until a clock says otherwise: once the phone has set the time and the last photo's epoch is known, the epoch difference wins. A camera that was sad when the battery died wakes up equally sad, not sadder, and catches up the moment a phone tells it the date.
- The unix clock and the phone's UTC offset arrive over BLE (`SET_TIME`, `docs/protocol.md` §3). Until they have once, the camera knows durations but not the time of day. If the clock arrives after the photo, the photo's time is back-filled from the elapsed seconds.

## Sleep face

The sleep face is 16 px block art, white on black, drawn in code (`Display::drawSleep(happiness, breathIn, faceOnly)`), one face per band:

| band | eyes | mouth |
|---|---|---|
| happy | ∪ closed, corners up | a three-block smile |
| content | ∪ | the original single dot |
| glum | ∪ | a flat line |
| sad | ∩, turned over | a three-block frown |

It **breathes**: every 5 s the other of two frames goes up, eyes two pixels higher and mouth four pixels lower, so the distance between them grows and shrinks. Awake behind the sleep face that is a timer in `loop()`; in light sleep it rides the 5 s VCOM wake that exists anyway. A breath pushes only the face rows to the panel; a full frame is sent when the face first goes up and when the band changes.

Preview without hardware: `cd firmware/tools/preview && make scenes` renders `sleep`, `sleep-content`, `sleep-glum`, `sleep-sad` and `sleep-breath`.

## Sounds

### When

- **The one-hour chirp.** 55–65 min after the last photo (drawn once, at the photo), one chirp at the current happiness. Once per photo. If that moment falls at night it is dropped, not moved.
- **Attention chirps.** From 24 h after the last photo. The first comes 0–8 h after the day mark; every next one is drawn 12–20 h after the previous, so **no 24-hour window ever holds more than two**, and the spacing is never the same twice. Appointments are stored in NVS with the rest of the mood state, so a reset does not re-roll them.
- **Not at night.** No chirp between 22:00 and 08:00 local time **when the camera knows local time** (the phone sent the clock and its UTC offset). A chirp that comes due in that window is moved to 08:00 plus 0–2 h. Without a clock the night rule is skipped and only the two-a-day cap applies. Decided deliberately: a camera that has never met a phone should still be heard.
- **Only on the viewfinder or the sleep face,** with the button up, no melody playing and no transfer running. A due chirp never interrupts the save screen, the gallery or a slide; it waits for the next opportunity.
- **From light sleep:** the sleep loop wakes every `Display::VCOM_INTERVAL_MS` (5 s) to flip VCOM; the mood is serviced on that wake and a due chirp plays right there, radio and camera being down anyway.
- **A tap on the sleeping camera** (released before the one-second wake hold), awake or in light sleep, answers with a chirp at a **random** happiness. It is feedback, not mood: it tells you that you pressed the button, it does not wake the camera and does not count as activity. Nothing else on the camera plays a chirp on purpose.

### What

Chirps come from a small procedural composer, not from a list of fixed melodies, so no two are the same.

- **At most 300 ms** in total, the length of the existing ta-da-da. Most are shorter.
- A chirp is **1–3 segments**, each a **tone**, a **sweep** (glide from one frequency to another) or a **noise** burst, each with its own random length (tones 20–120 ms, sweeps 40–150 ms, noise 10–60 ms) and a random gap after it (0–40 ms).
- **Happy (happiness near 1):** tones only, or one sweep between two notes. All pitches are on the **A major scale** between A5 and A7 (880–3520 Hz, where the piezo is loudest). Successive notes move by a step, a third or a fifth, so a chirp is a tiny motif rather than random pitches.
- **Getting sad:** with a probability that grows with sadness, notes are pushed off the scale by a semitone, or detuned by up to ±50 cents; sweeps get longer and may fall as well as rise; noise bursts appear once happiness is below ~0.75.
- **Sad (happiness near 0):** most segments are sweeps or noise, spans up to an octave in either direction, few notes on the scale.
- The whole score is a pure function of a random seed and the happiness, so a chirp can be reproduced on the host. `firmware/tools/chirp` renders scores to WAV files for listening on a laptop; `firmware/tools/hosttest` checks the invariants (length cap, pitch range, scale membership when happy).

### How it is played

Bit-banged square waves on the buzzer pin, like the shutter click, because the LEDC `tone()` path is unreliable for short sounds right after a wake. A chirp therefore blocks the main loop for its length (≤300 ms). That is accepted: the radio runs in its own task, the wake gesture is a one-second hold, and the screens a chirp is allowed on are static or a live preview.

## Testing it

`firmware/tools/hosttest` runs a simulated month through the state machine at the 5-second tick and checks every rule above. On the bench, `-DLC_MOOD_FAST` in `platformio.ini` makes the mood clock run 600× (an hour in six seconds, the week in about seventeen minutes) so the whole arc, faces included, plays out in one sitting. Every chirp and every state save is a line on the serial monitor (`Sleep: chirp happiness=…`, `Mood: saved (attention chirp) …`).

## Open points

- The breath costs a partial frame push every 5 s in light sleep. There is no current sense on the board; if the idle draw turns out to matter, the breath period is one constant.
- Whether the one-hour chirp should also play while the phone is actively syncing (currently: it waits).
