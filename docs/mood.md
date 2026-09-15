# Mood

The camera wants to be used. Left alone it gets a little anxious over the course of a week and asks for attention, the way a tamagotchi would, but quieter. Everything below keys off one number: **how long ago the last photo was taken.**

Status: the **sound engine** exists (`firmware/src/chirp.h`, `Audio::playChirp`). The mood clock, the scheduler and the face changes are specified here and not built yet. Until they are, a tap in the gallery plays a chirp at a random happiness so the engine can be heard on hardware.

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
- Across a **power loss** (dead battery, cable pulled), unknown. The camera has no clock battery. Rule: mood state is persisted in NVS (`Preferences("mood")`: the last photo's unix time when the clock was known, otherwise the last computed happiness) and resumed on boot. Time spent powered off does not count. A camera that was sad when the battery died wakes up equally sad, not sadder.
- The unix clock arrives from the phone over BLE (`SET_TIME`, `docs/protocol.md` §3). Until it has once, the camera knows durations but not the time of day.

## Sleep face

The sleep face (`Display::drawSleep()`, bitmap in `firmware/src/sleep_data.h`) is redrawn on every entry to `Asleep` and on every VCOM tick while light-sleeping, so it can follow the mood without extra wakes.

Planned: the mouth is the only part that changes. Three states (smile, flat, down) drawn procedurally over the bitmap's mouth region, chosen from the happiness bands above; the eyes stay closed and everything else stays as it is. To be verified with `tools/preview` (`sleep --happiness N`) before touching hardware, as with every screen.

## Sounds

### When

- **The one-hour chirp.** About an hour (55–65 min, drawn once) after the last photo, one chirp at the current happiness. Once per photo, never repeated.
- **Attention chirps.** From 24 h after the last photo: chirps at random intervals, **at most two per calendar day**, spaced at least four hours apart. The next chirp time is drawn when the previous one plays (or at boot), stored in NVS with the rest of the mood state so a reset does not re-roll it.
- **Not at night.** No attention chirps between 22:00 and 08:00 local time **when the clock is known**. Without a clock (the phone has never connected this boot and nothing is persisted), the night rule is skipped and only the twice-a-day cap applies. Decision taken deliberately: a camera that has never met a phone should still be heard. The timezone comes with the clock; `SET_TIME` carries UTC today and needs a UTC offset added (open point below).
- **Only while asleep or in the viewfinder.** A chirp never interrupts the save screen, the gallery, a melody or a transfer; it waits for the next opportunity.
- **From light sleep:** the sleep loop already wakes every `Display::VCOM_INTERVAL_MS` (5 s) to flip VCOM. A due chirp plays on that wake (`Audio::init`, play, back to sleep), the same way the wake click is bit-banged so it is heard straight out of sleep.

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

## Open points

- `SET_TIME` needs a UTC offset (or the phone sends local time) before the night rule can work. Protocol change, small; the app is the only client.
- The sleep face has no vector source; the mouth variants will be drawn in code over the bitmap.
- Whether the one-hour chirp should also play while the phone is actively syncing (currently: it waits).
