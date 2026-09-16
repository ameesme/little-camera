# AGENTS.md

Project context for coding agents. Read this before touching firmware, pin config, or hardware-facing code. The constraints below are load-bearing — violating them either bricks a peripheral or silently breaks the display.

## Project

Standalone, battery-powered cellular IoT camera. It captures a small number of low-resolution grayscale photos per day, uploads them over 4G LTE-M, periodically polls for incoming images, shows a live grayscale viewfinder plus received images on a monochrome Sharp Memory LCD, and gives audio + button feedback. Firmware runs on an ESP32-S3.

The prototype moved from a LilyGO S3_CAM_SIM dev board to a **custom carrier PCB (Xiao_Shutter)**. The carrier hosts the display, buzzer, and shutter button; the camera is the OV2640 on the **XIAO ESP32-S3 Sense expansion** (B2B connector). **Cellular is not on this carrier revision** — integration is pending.

Guiding philosophy: pragmatic and buildable over theoretically optimal. Tradeoffs are made explicitly and documented. Push back on over-engineering rather than adding complexity by default.

## Hardware stack

- **Carrier:** Xiao_Shutter custom PCB
- **MCU module:** Seeed XIAO ESP32-S3 Sense (U1, 14 castellated pads; camera + microSD on the Sense expansion via B2B)
- **Display:** Adafruit 4694 Sharp Memory LCD (LS027B7DH01), 2.7", 400×240, monochrome — on 1×9 header U2
- **Audio:** passive buzzer on 1×3 header BZ1 (S/V/G)
- **Input:** Omron D2F micro-switch (SW1) — shutter button, presses through the housing
- **Spare GPIO:** J2, 1×6 SMD pads — silk reads GND · D6 · D5 · D4 · D3 · 3V3

## Pin allocation — DO NOT REASSIGN

These come from the carrier PCB netlist. Do not move a function without an explicit request.

| Function | GPIO | XIAO pad | Notes |
|---|---|---|---|
| LCD SCLK | GPIO7 | D8 | datasheet max 2 MHz; run at 8 MHz (see constraints) |
| LCD MOSI | GPIO9 | D10 | write-only bus, no MISO |
| LCD CS | GPIO44 | D7 | **ACTIVE HIGH** |
| LCD DISP | GPIO3 | D2 | display enable — drive HIGH |
| Shutter button | GPIO1 | D0 | `INPUT_PULLUP`; **inverted vs. netlist: pressed = HIGH** (see constraints) |
| Buzzer SIG | GPIO2 | D1 | |
| Spare (J2) | GPIO4 / GPIO5 / GPIO6 / GPIO43 | D3 / D4 / D5 / D6 | D4=default SDA, D5=default SCL, D6=default UART TX |
| Reserved | GPIO8 | D9 | SPI MISO — kept free for the Sense board's microSD |

LCD header (U2) hardware notes: EXTIN (pin 1) and EXTMD (pin 3) are tied to GND on the carrier; pin 8 (breakout's 3V3 regulator output) is **not connected — never drive it**; VIN (pin 9) is fed from 3V3.

## Hard constraints

Treat each of these as a hard failure if violated:

- **Sharp Memory LCD specifics** (LS027B7DH01):
  - CS is **active-high**, plain GPIO — not a normal SPI CS. Assert high to talk to it.
  - SPI is **LSB-first, mode 0**. Datasheet max SCLK is 2 MHz, but the firmware deliberately runs **8 MHz** — proven stable on this panel since the LilyGO board, and 2 MHz makes frame pushes (~50ms each) laggy. If the panel glitches, reduce the clock before suspecting anything else.
  - **Write-only** — there is no MISO. Never attempt a read transaction.
  - VCOM is handled in **software**; EXTIN and EXTMD are tied low on the carrier. Firmware must toggle VCOM periodically to prevent DC bias / image burn-in.
  - DISP is GPIO-driven (GPIO3) on this carrier — it must be driven HIGH or the panel shows nothing.
- **SW1 has no external pull-up or debounce** — do both in firmware (`INPUT_PULLUP` + software debounce). Scope-equivalent measurement on GPIO1: line is silent at rest, real presses hold ~200ms, release bounce lasts ~1ms. A single 10ms resample (`confirmPressed()` in main.cpp) covers that gap — don't stack extra edge-timing windows on top of it.
- **SW1 polarity is inverted on this carrier revision** — measured behavior is released = solid LOW, pressed = HIGH (line floats to the internal pull-up), consistent with the D2F's NC terminal routed instead of NO. Firmware treats HIGH as pressed (`shutterPressed()` in main.cpp); light-sleep wake is HIGH-level. Verify with a meter and fix the routing on the next board spin, then flip the firmware back.
- **Leave GPIO8 (D9/MISO) free.** It is reserved for the Sense board's microSD.

## Firmware architecture

- **Camera** (when the Sense module lands): `PIXFORMAT_GRAYSCALE`, QVGA 320×240 (~77KB/frame). Framebuffer lives in **internal DRAM, not PSRAM** — keep it that way unless there's a measured reason to change.
- **Live viewfinder:** ordered **Bayer** dithering — stable and fast under motion, no shimmer.
- **Received / stored images:** **Floyd–Steinberg** dithering for better static-image quality.
- **Display output:** source is 320×240; pillarbox to the 400×240 panel.
- **UI mode machine** (`Mode` in main.cpp). One button drives everything, so what a press means depends entirely on the screen. Gesture table: **press = shoot**, **hold 700ms (`GALLERY_HOLD_MS`) = gallery in/out**, **hold 1500ms (`TRASH_HOLD_MS`) = trash** on the save screen, **hold 1000ms (`WAKE_HOLD_MS`) = wake** a sleeping camera (taps are ignored; the wake fires at the one-second mark, not on release). Every hold compares against a fresh `millis()` (see the Save note) and fires *on the threshold*, not on release.
  - `Viewfinder` — live Bayer preview, sidebar on the left (`photos / hold`). Press shoots.
  - `Capture` — frozen Floyd–Steinberg frame, same layout. **Grab on press, commit on release:** the frame is captured and drawn on the press edge exactly as before (zero shutter latency), but while `captureHeld` is true the dwell/slide waits. Released before `GALLERY_HOLD_MS` → a shot, and the slide starts (from the dwell mark if the hold outlasted it). Still down at `GALLERY_HOLD_MS` → the frame is dropped and the gallery opens. A normal ~200ms tap is pixel-identical to the old behaviour because the 350ms dwell covers it. After that: holds for `CAPTURE_DWELL_MS`, then slides to the save layout over `SAVE_SLIDE_MS` (ease-out cubic, driven from main.cpp — `Display::drawSaveTransition(t)` takes a linear 0–1 position and knows nothing about the curve).
  - `Save` — photo shifts flush left, send/trash column takes the freed 80px on the right. Press = send (writes to flash, see below), hold `TRASH_HOLD_MS` (1500ms) = trash. Compare the hold against a fresh `millis()`, **not** the `now` sampled at the top of `loop()` — `confirmPressed()` burns 10ms, so `pressStartedAt` ends up later than `now` and the unsigned subtraction wraps, firing trash on every press. That bug shipped once already.
  - `Gallery` — one stored photo in the save geometry (card flush left) with the browse column on the right: `→ 3/12 / press` and `back / hold`. Press = next (newest → oldest, wraps), hold = back to the viewfinder. Nothing else — no delete, no zoom; the phone does housekeeping. Both entering and leaving happen mid-hold, and both the gallery and the viewfinder only act on a *press edge*, so the hold that opened the gallery never advances it and the hold that closed it never shoots. The screen is static: it repaints only when the photo changes or a toast appears/expires (`Display::toastVisible()`). The decoded bitmap lives in `galleryBits` in main.cpp; `Display::drawGallery(bits, index, total)` takes it by pointer and never touches storage, so `tools/preview` can render `gallery` and `gallery-empty` scenes (with `--pbm FILE` for a real photo).
  - `Asleep` — what the idle timeout (10s; 30s in the gallery and on the save screen, `REVIEW_IDLE_TIMEOUT_MS`) leads to: the sleep face goes up and the camera driver comes down at once, but the chip stays awake with the radio advertising for `RADIO_LINGER_MS` (30s), stretched while a phone is active (`Sync::activeRecently`) or the console is in use. Nothing is drawn while asleep except the face itself (it follows the mood and breathes every `BREATH_MS`, see Mood) and a pairing code, which takes over the screen without waking the camera and hands it back when pairing ends. Hold `WAKE_HOLD_MS` → viewfinder; a **tap chirps in the camera's current mood** (so an accidental press is heard, and says how it is doing) and is not activity: it neither wakes nor stretches the radio window. Then `enterSleepMode()` (real light sleep) runs as before.
  - `Pairing` — the six-digit passkey, full screen, while the phone asks for it (`PAIRING_TIMEOUT_MS`, 60s), then the outcome for `PAIRING_RESULT_MS` before the screen goes back where it came from (the sleep face if that is what was up, otherwise the viewfinder). The button does nothing here and the idle timeout is suspended. **The radio has no other UI**: connecting and transferring are the phone's business and raise no toast, which is also why the passkey is a screen and not one — the viewfinder's own "press to shoot" hint used to paint straight over it.
  - **Toasts and screen switches:** a timed toast only expires when something re-renders. Every mode transition therefore calls `Display::clearToast()`, and the two static screens (Save, Gallery) repaint when `Display::toastVisible()` changes so a toast raised on them also goes away. The viewfinder re-renders every frame and needs neither.
  - `Display::drawSave()` only ever acts on the frame you just shot; the gallery is the separate screen for stored photos.
  - `Camera::capture()` hands back the driver's framebuffer and recycles it on the next call, so `Capture`/`Save` must never grab a new frame. Sleep frees it, hence the reset to `Viewfinder` in `enterSleepMode()`.
- **Photo storage** (`storage.{h,cpp}`): LittleFS on the partition table's 1.5MB `spiffs` entry — that's a label, not the format; LittleFS is picked because it survives power loss mid-write. Files are binary **PBM (P4)**, i.e. the packed 1-bit rows `Display::photoBits()` already holds behind a short ASCII header, so they open in any viewer for free. Things to know:
  - PBM sets a bit for **black**; the panel sets it for **white**. Rows are inverted on write and on read.
  - The header carries one comment line, `# boot=17 up=48213 t=1757789000` (boot counter, `millis()` at capture, unix time or 0 if the clock was never set this boot). Written by `Pbm::writeHeader()` in `pbm_header.h`, parsed by `Pbm::parseHeader()`. Files from older firmware have no comment line and must keep parsing. Format spec: `docs/protocol.md` §2.
  - **Synced photos are renamed** `/0007.pbm` → `/0007s.pbm` when the phone ACKs them over BLE. That suffix is the only sync bookkeeping; it lives with the file, so it survives everything. `photoAt(ordinal)` is newest-first.
  - **Eviction only touches synced photos.** When the partition is full, `savePhoto()` deletes the oldest `s` files until the new one fits; if none are left it returns `Result::Full` and the review screen says `full, sync first`. The original ring deleted anything; that was acceptable only while nothing could get photos off the device.
  - The index table is built from a directory scan at boot, not an NVS counter, so it can't drift from what's actually on disk. The boot counter *is* in NVS (`Preferences("storage").boot`).
  - **Frozen for the sake of photos already on devices:** the partition table (`default_8MB.csv`), the mount call (`LittleFS.begin(true, "/littlefs", 10, "spiffs")`), the file naming and the P4 layout. Never `erase_flash`, never switch to `huge_app.csv`; `pio run -t upload` only rewrites the app slot and leaves photos alone. Back up first with `tools/export/dump_flash.py` regardless.
  - **Saving is 1-bit, so the greyscale is gone.** That is intended: the blog shows the dithered frame exactly as the panel did.
- **Getting photos off:** three ways. `tools/export/dump_flash.py` reads the partition with esptool (any firmware). The USB console (`console.{h,cpp}`: `ls`, `get N`, `stat`, spec in `docs/protocol.md` §6) with `tools/export/pull_serial.py`. And the BLE sync service used by the iOS bridge app.
- **Identity** (`identity.{h,cpp}`): camera id = Bluetooth MAC as 12 hex; short code derived from it (`shortcode.h`, vectors in `docs/protocol.md` §1, checked by `tools/hosttest`); 16-byte secret minted once into NVS (`Preferences("sync").secret`). The secret is what the app presents to the server as the camera — only ever hand it out over an encrypted link.
- **Audio** (`audio.{h,cpp}`): three kinds of sound, two ways of making them. The **click** (shutter, wake, back to the viewfinder; a lighter tick into the gallery) and the **chirps** are bit-banged square waves on the buzzer pin (`digitalWrite` + `delayMicroseconds`, blocking) because the LEDC `tone()` path produced nothing for short sounds right after a light-sleep wake. The **melodies** (ta-da-da, its reverse for sleep, the chirp-chirp, the A-A'-E "saved") still use `tone()` from the async `Audio::update()` machine. Nothing is longer than 300ms; ta-da-da is exactly that and it is the ceiling for everything else.
  - **Chirps** are composed, not listed: `chirp.h` (Arduino-free) turns a happiness level 0-255 plus a seed into a 2-3 segment score (never a single beep, never under `MIN_TOTAL_MS` = 100ms) of tones, sweeps and sample-and-hold noise, on the A major scale and rising when happy, detuned, off-scale, falling and noisy when sad; `Audio::playChirp()` plays it. Who asks for one and when is the Mood bullet below; every chirp is logged before it plays (`Sleep: chirp happiness=173/255 band=2 T1661/44 +12 S1661>2637/80 = 136ms`).
  - Audition on the host first: `cd tools/chirp && make bands` renders the four bands and a 255→0 ladder to `out/*.wav` through the very same `Chirp::play()` loops the pin runs. `tools/hosttest` pins the invariants (≤300ms, pitch range, on-scale when happy, played length within one wave period of the score).
  - Chirps use `chirp.h`'s own xorshift so a score renders identically on the host; the click keeps Arduino's `random()`. Don't unify them.
- **Mood** (`mood.h`, glue at the top of main.cpp, NVS in `mood_store.{h,cpp}`, behaviour in `docs/mood.md`): happiness 255→0 over a week since the last photo; one chirp 55-65 min after a photo, then attention chirps from the day mark, the next drawn 12-20 h after the previous (so never more than two in 24 h), deferred to 08:00-10:00 local when they would fall between 22:00 and 08:00 and the camera knows local time (SET_TIME with a UTC offset; without one there is no night). Rules:
  - `mood.h` is a pure state machine over a `Mood::Clock` the caller builds; it never touches hardware. `serviceMood()` in main.cpp runs it every loop iteration and on every VCOM wake in light sleep, and only lets a chirp play on the viewfinder or the sleep face, with the button up, no melody and no transfer. Never chirp on Save, Gallery or a slide.
  - The mood clock is uptime from `esp_timer` (64-bit, runs through light sleep). It resumes from the NVS blob (`Preferences("mood").state`, 17 bytes, versioned) across resets. Once the phone has set the wall clock and the last photo's epoch is known, the epoch difference wins, so time powered off counts too; until then it does not.
  - NVS is written on a photo, a chirp, a clock change and otherwise every 15 min (`Mood::persistDue`). Do not write it per tick.
  - The sleep face (`Display::drawSleep(happiness, breathIn, faceOnly)`) is 16px block art drawn in code, one face per `Chirp::band`: the eyes carry the happiness (lifted corners only in the top band, flat below it) and the mouth carries the rest (a square until it stretches flat, then turns down); a full frame when it goes up or the band moves, face rows only for the breath frame every 5 s (`BREATH_MS` awake, the VCOM tick asleep). Preview scenes `sleep`, `sleep-content`, `sleep-glum`, `sleep-sad`, `sleep-breath`.
  - `-DLC_MOOD_FAST` in platformio.ini runs the mood clock 600x for bench tests. Off by default.
- **Host checks:** `tools/hosttest/make test` runs the Arduino-free headers (PBM header codec, CRC-32, SHA-256, short code, chirp composer, a month of the mood machine) against known vectors; `make check` syntax-compiles audio/mood_store/storage/console/identity/sync/main against stubbed Arduino headers. Neither replaces `pio run`, but both run anywhere.
- **BLE sync** (`sync.{h,cpp}`, layout in `sync_protocol.h`, contract in `docs/protocol.md` §3): NimBLE-Arduino 2.x peripheral, one service, four characteristics (Info / Secret / Control / Data). The phone lists, pulls files as notification frames, and ACKs; an ACK renames the file to its `s` form. Rules:
  - **Nothing runs in NimBLE callbacks** except stashing the write and stamping `_lastBleActivity`. All storage and display work happens in `Sync::loop()` on the main thread, and UI is raised through `Sync::nextEvent()` → toasts in `main.cpp`. Do not draw or touch LittleFS from the BLE task.
  - **Radio only while awake.** `Sync::end()` (deinit) runs before `Camera::deinit()` in `enterSleepMode()`, `Sync::begin()` after the camera comes back. Light sleep with a live controller is not something this core supports; don't try to keep the link across sleep.
  - **When the radio runs:** while awake and for 30s behind the sleep face (`RADIO_LINGER_MS` in main.cpp), stretched while a connected phone has done something in the last 60s (`Sync::activeRecently`, `BLE_ACTIVE_MS` in sync.cpp). The shutter always wins; a transfer just continues alongside.
  - **Secret** is `READ_ENC | READ_AUTHEN` with passkey display: a random 6-digit code, shown on the `Pairing` screen (see the mode list) rather than as a toast. If passkey pairing proves flaky on some phone, the fallback is Just Works (`READ_ENC` only, `BLE_HS_IO_NO_INPUT_OUTPUT`) — still stops passive readers.
  - Info is re-packed from `loop()` every 500ms (never from the read callback), so its uptime is at most that stale.
  - `platformio.ini` disables the central/observer roles and caps connections at 1. `default_8MB.csv` stays; NimBLE + camera fit the 3.3MB app slot with room.
  - Verified so far: `tools/hosttest` (frame/Info/entry layouts against the doc, the dating rule) and `make check` against a NimBLE stub. **Not yet verified on hardware**: pairing UX, iOS MTU, deinit/init across sleep, transfer speed. Those are the first things to test when a board is at hand.
- **Sleep:** light sleep, entered once the radio window behind the sleep face closes, woken by the shutter (HIGH level) or a `Display::VCOM_INTERVAL_MS` timer. The sleep loop leaves **only on a GPIO wake held for `WAKE_HOLD_MS`** (a tap chirps and goes straight back to sleep); a timer wake toggles VCOM, breathes the face and services the mood (a due chirp plays right there, radio and camera being down), and any other cause (BT, undefined, a rejected `esp_light_sleep_start()`) is logged and slept through — treating "anything but the timer" as a press made the camera wake by itself once the radio existed. `Sync::end()` forces the BT controller off and clears its wake source before sleeping for the same reason. Two more things are load-bearing — don't remove them:
  - The panel keeps showing the sleep face (DISP stays HIGH across light sleep), so the timer wake exists purely to flip VCOM. Without it the pixels sit at fixed DC polarity for the entire sleep and burn in.
  - `Camera::deinit()` before sleeping, `Camera::init()` after waking. PWDN and RESET are both `-1` on the Sense B2B connector, so light sleep only gates XCLK — the OV2640 stays powered and biased at milliamps, dominating the idle budget (sleeping S3 ~250µA, static panel ~50µA). Costs a few hundred ms of re-init on wake.
  - Beyond that, don't chase deep-sleep micro-optimizations at the cost of prototype complexity. There is no current sense on this board, so every power figure above is an estimate, not a measurement.

## Build & flash

PlatformIO + Arduino framework. Environment is `xiao_shutter`.

- **Build:** `pio run -e xiao_shutter`
- **Upload:** `pio run -e xiao_shutter -t upload`
- **Monitor:** `pio device monitor`
- **Build + upload + monitor:** `pio run -e xiao_shutter -t upload -t monitor`

Config facts that affect firmware:

- Board `seeed_xiao_esp32s3`, MCU esp32s3, **8MB flash**, partitions `default_8MB.csv`, QIO flash + OPI PSRAM (`BOARD_HAS_PSRAM`).
- `ARDUINO_USB_MODE=1` + `ARDUINO_USB_CDC_ON_BOOT=1`: **`Serial` is the native USB-CDC console**, not a UART bridge. The USB port re-enumerates after flashing, so the monitor may need a moment to reconnect.

## UI preview (no hardware)

`tools/preview/` compiles the **real** `src/display.cpp` for the host against a stub Arduino/SPI layer and writes what the panel would be showing to a BMP. **Iterate on layout here before flashing** — once the app starts light-sleeping, every upload needs a physical shutter press to catch the board awake.

Host toolchain only (`c++`, no PlatformIO, no deps). Nothing under `src/` is modified or conditionally compiled for it.

```
cd tools/preview
make            # build ./preview
make scenes     # render every scene to out/
make open       # render every scene and open them (macOS)
make clean
```

```
./preview <scene> [options]

scenes:   viewfinder  capture  save  slide  toast  sleep  splash  gallery  gallery-empty
  --out PATH   output BMP            (default preview.bmp)
  --scale N    integer upscale       (default 2 — 1:1 is unreadable on hidpi)
  --src FILE   320x240 binary PGM    (default: built-in synthetic test image)
  --at MS      virtual clock value   (default 0)
  --t F        slide position 0-1     (default 0.5, 'slide' scene only)
  --pbm FILE   320x240 binary PBM     (a photo pulled off the camera, 'gallery' scene)
  --index N    gallery counter position (default 2)
  --total N    gallery counter total    (default 12)
```

Layout:

| File | Role |
|---|---|
| `preview.cpp` | CLI, scene list, test-image generator, PGM loader |
| `sim_panel.{h,cpp}` | LS027B7DH01 model — decodes the SPI stream, writes the BMP |
| `shim/Arduino.h` | `millis`, `digitalWrite`, `PROGMEM`, `Serial`, … |
| `shim/SPI.h` | `SPIClass` that forwards every byte to the panel model |
| `sim_arduino.cpp` | Virtual clock + `Serial` backing |

How it works, and how far to trust it:

- It intercepts at the **SPI byte stream**, not the framebuffer, so it exercises the real wire protocol — command bits, VCOM bit, line addressing, bit order. That's why `drawSplash()` previews correctly even though it streams PROGMEM straight to SPI and never touches `_framebuffer`.
- `millis()` is a **virtual clock** (`--at`, or `Sim::advanceMillis` in-process), so time-dependent UI — toast expiry, animation — is reproducible and can be stepped frame by frame into an image sequence.
- It warns on **DISP low** and on **SPI protocol errors**, which covers two of this panel's classic blank-screen causes.
- It models the **digital side only**. No contrast, no 8MHz-overclock behaviour, no refresh timing, no ghosting. Layout and dithering are faithful; "will the panel actually like this" is still a hardware question.

Extending it: add a branch in `preview.cpp` for a new scene. The Arduino shim deliberately covers only the symbols `display.cpp` actually uses — widen it when a build fails rather than emulating Arduino wholesale.

## Open tasks / known unknowns

Flag these if relevant to a change; don't silently assume they're resolved:

- **Mood is untested on hardware** — the scheduler is host-tested over a simulated month, the faces are previewed, but the first real week (NVS resume, chirps out of light sleep, the breath's power cost — estimated at a few hundred µA average for the face rows every 5 s, unmeasured) is still ahead. `-DLC_MOOD_FAST` compresses it to a bench session.
- **Cellular/modem** is not on this carrier revision — the previous SIM7080G integration (AT+CBC battery readout, AT+CPOWD power-down) is retired with the LilyGO board and needs a new home. Until it lands, the iOS bridge app is the uplink (see `docs/protocol.md`).
- **BLE sync is untested on hardware** — written against NimBLE-Arduino 2.x and checked on the host only. First things to verify with a board: `pio run` size line, pairing with the passkey toast, an end-to-end pull from the app, and that the stack comes back cleanly after a light-sleep cycle.
- **Battery/power path** for the carrier (XIAO BAT pads vs. external charger) is undecided.
- **Enclosure** is unresolved for the new board.
- Cosmetic boot logs: `spiAttachMISO(): HSPI Does not have default pins on ESP32S3!` (we pass -1 for MISO — panel is write-only) and a one-shot `ledc_get_duty(): LEDC is not initialized` from the tone() HAL. Both harmless.

## Conventions

- Justify non-obvious decisions in a comment with the tradeoff, matching how the rest of the project is documented.
- Prefer the simplest thing that works on real hardware. If a change adds complexity, say what it buys and what it costs.
