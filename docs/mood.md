# Mood

The camera wants to be used. Left alone it gets anxious over a week and asks for attention, the way a tamagotchi would but quieter. Everything keys off one number: **how long ago the last photo was taken.** Built and running on board 1, host-tested over a simulated month, but the first real week on hardware is still ahead.

`happiness` is 1.0 the moment a photo is saved and falls linearly to 0.0 over 7 days without one. Taking a photo resets all of it.

| Since last photo | Happiness | Face | Sound |
|---|---|---|---|
| 0 – 1 h | 1.0 – 0.99 | asleep, eyes lifted | one chirp at ~1 h, happy |
| 1 h – 24 h | 0.99 – 0.86 | eyes lifted | none |
| 1 – 3 d | 0.86 – 0.57 | eyes go flat | harmonic, with the odd slide |
| 3 – 5 d | 0.57 – 0.29 | mouth stretches flat | off-scale, a sweep or short hiss creeps in |
| 5 – 7 d | 0.29 – 0.0 | mouth turned down | dissonant, noisy, sweeps both ways |
| > 7 d | 0.0 | as sad as it gets | the same, no further escalation |

The eyes carry the happiness, so the happy face needs no smile — just lifted eyes over the same square mouth. Below the top band they go flat and stay flat and the mouth does the rest. The face breathes for its first two minutes, and a tap restarts that window, since it means someone is there. After it the sleep tick stretches to 60 s and nothing is drawn at all.

## The rules

- **Time spent powered off does not count.** There is no clock battery, so mood state is persisted in NVS and resumed on boot: a camera that was sad when the battery died wakes up just as sad as it was, and catches up as soon as a phone sends the date. Durations are known from the first boot; the time of day only after a `SET_TIME` with a UTC offset (`protocol.md` §3).
- **No more than two attention chirps in any 24 hours**, and never the same spacing twice, so it does not turn into an alarm clock. Appointments are stored with the mood state, so a reset does not re-roll them.
- **Nothing between 22:00 and 08:00 local**, when local time is known. Without a clock the night rule is skipped and the chirp still plays. That is deliberate: a camera that has never met a phone should still be heard.
- **A chirp waits for a quiet screen.** Only the viewfinder or the sleep face, button up, nothing playing, no transfer running. It never interrupts the save screen, the gallery or a slide.
- **A tap on a sleeping camera answers in its current mood** without waking it and without counting as activity, so a press in a bag both explains itself and says how the camera is doing.
- **Every chirp is composed on the spot.** The score is a pure function of a seed and the happiness, so any chirp can be reproduced on a laptop: `tools/chirp` renders scores to WAV and `tools/hosttest` checks the invariants (≤300 ms, 2–3 segments, the A major scale while happy).

## Testing it

`tools/hosttest` runs a simulated month through the state machine and checks every rule above. On the bench, `-DLC_MOOD_FAST` runs the mood clock 600× — the week in about seventeen minutes — so the whole arc plays out in one sitting, faces included. Every chirp and state save is a serial line.

Open: whether the one-hour chirp should also play while the phone is syncing. It waits today.
