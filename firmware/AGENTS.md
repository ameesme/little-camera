# AGENTS.md

Project context for coding agents. Read this before touching firmware, pin config, or hardware-facing code. The constraints below are load-bearing — violating them either bricks a peripheral or silently breaks the display.

## Project

A small battery-powered camera. It shoots a handful of 1-bit photos a day, shows a live dithered viewfinder and the stored photos on a monochrome Sharp Memory LCD, chirps about how long it has been ignored, and hands its photos to a phone over BLE. Firmware is Arduino-on-ESP32-S3.

There is no modem. **The iOS bridge app is the uplink** (`docs/protocol.md`); the camera never talks to the network itself.

Guiding philosophy: pragmatic and buildable over theoretically optimal. Tradeoffs are made explicitly and documented. Push back on over-engineering rather than adding complexity by default.

## Board: little camera v3q

The current board is the **v3q custom PCB** — one board, no carrier and no module. Everything is on it: the S3, the camera connector, the display header, the charger and the battery sense.

- **MCU:** ESP32-S3-WROOM-1-N16R8 — **16 MB** quad flash, 8 MB octal PSRAM
- **Display:** Sharp LS027B7DH01, 2.7", 400×240 monochrome, on J3 (330R series guard on every line)
- **Camera:** OV2640 DVP on J2, rails gated by `CAM_EN`. J2 also takes an OV3660
- **Audio:** buzzer to +3V3 switched low-side by Q2 (DMG1012UW)
- **Input:** one D2MQ micro-switch, NO to GND, with a hardware pull-up and RC
- **Power:** USB-C, TP4054 single-cell charger, VBUS / CHRG / VBAT all readable by firmware
- **Spare:** J4 I²C pads (4k7 pull-ups, nothing on board), one dock pad

**Source of truth for the hardware is `selftest/` plus the v3q netlist (`little_camera_v3/gen/design.py`).** The self-test is standalone on purpose — it shares no code with `src/`, so a board that fails it points at the hardware, not at the app. Run it on every new board before flashing the app:

```
cd firmware/selftest && pio run -t upload -t monitor
```

Five stages, driven by the shutter (press = next, hold 1 s = the action named at the bottom), mirrored on USB serial — `?` lists the serial commands. Display, buzzer, camera, button, then a PASS/FAIL/?? summary of everything firmware can judge by itself.

## Pin allocation — DO NOT REASSIGN

From the v3q netlist. Do not move a function without an explicit request.

| Function | GPIO | Notes |
|---|---|---|
| LCD SCLK | 4 | datasheet max 2 MHz; run at 8 MHz (see constraints) |
| LCD SI | 5 | write-only bus, no MISO |
| LCD SCS | 6 | **ACTIVE HIGH** |
| LCD DISP | 7 | HIGH = panel shows |
| Shutter | 44 | D2MQ NO→GND, R26 10k pull-up, C24 100n — **pressed = LOW** |
| Buzzer | 46 | Q2 gate, R27/R28 — **must idle LOW** |
| VBUS sense | 16 | VBUS × 51/84 (R8 33k / R9 51k) — a logic input, not a voltage |
| CHG | 17 | TP4054 CHRG through a divider: LOW = charging |
| VBAT sense | 3 | VBAT / 2 (R34/R35 1M/1M, C7 100n) |
| I²C SDA / SCL | 1 / 2 | J4 pads 4/5, 4k7 pull-ups, nothing on board |
| Dock / fob | 43 | dock pad 2, flash fob trigger, U0TXD — idles high, **leave alone** |
| CAM_EN | 45 | enables U4/U5/U6, R12 pull-down |
| CAM_PWDN | 11 | |
| CAM_RESET | 9 | R14/C14 releases it ~1 ms after the rails come up |
| CAM_XCLK | 14 | |
| CAM_SIOD / SIOC | 18 / 8 | |
| CAM_VSYNC | 10 | |
| CAM_HREF | 12 | |
| CAM_PCLK | 48 | |
| CAM_D0–D7 | 39, 41, 42, 40, 38, 47, 21, 13 | `CAM_Dn` = sensor `Y(n+2)`, so CAM_D0 is the driver's `d0` |

## Migration status — `src/` is not on v3q yet

`selftest/` runs on v3q. **The app does not.** `src/` still carries the previous carrier's pin numbers and the assumptions that went with them. Until that is done, treat the app's pin constants as stale, not as a second opinion:

- `src/main.cpp:28-41` — LCD, buzzer and shutter constants, plus `shutterPressed()`
- `src/camera.cpp:9-26` — sixteen `#define`s, with `PWDN` and `RESET` at `-1`
- `platformio.ini` — the env is still named `xiao_shutter` on board `seeed_xiao_esp32s3`. Rename it to `v3q` and move to `esp32-s3-devkitc-1` with `memory_type = qio_opi`, as `selftest/platformio.ini` already does. **The XIAO board definition cannot stay**: it declares 8 MB flash and ships a `pins_arduino.h` that puts `LED_BUILTIN` on GPIO21, which is `CAM_D6` here.

Four things change behaviour, not just numbers:

1. **Shutter polarity flips.** The carrier had the switch's NC terminal routed, so the line idled LOW and pressed HIGH on an internal pull-up. v3q is wired as drawn: R26 pulls up, the switch pulls to GND, **pressed = LOW**. `pinMode` becomes plain `INPUT` (R26 is the pull-up), `shutterPressed()` inverts, and the light-sleep wake becomes `GPIO_INTR_LOW_LEVEL`. Debounce stays in firmware; C24 softens the edge but does not remove the ~1 ms release bounce.
2. **VCOM is hardware now.** U8 drives EXTCOMIN at ~6 Hz with EXTMODE tied high, so **the panel ignores the serial VCOM bit**. The app's software VCOM toggle is no longer what prevents burn-in — which means the light-sleep timer wake is no longer load-bearing *for the panel*. It still earns its keep driving the breath and servicing the mood, so keep it; just don't justify it with VCOM any more.
3. **The camera can actually be powered down.** `CAM_EN` gates three LDOs, and PWDN and RESET are both wired. On the carrier they were `-1`, so sleep only gated XCLK and the sensor sat biased at milliamps — the single largest item in the idle budget. Bringing the rails down in `Camera::deinit()` is the biggest power win available in this migration. Bring-up order from `selftest`: pins to high-Z, `CAM_EN` HIGH, wait 20 ms, `PWDN` LOW, `RESET` HIGH, then XCLK. Teardown is the reverse, and nothing may drive into an unpowered sensor.
4. **Battery and charge state are readable.** VBAT through a 1M/1M divider (×2 in software), VBUS and CHRG as logic levels. `selftest`'s `lipoPercent()` has a resting-voltage curve good enough for a readout. There is still **no current sense**, so power figures remain estimates.

Half the flash is also unreachable: the app and the self-test both use `default_8MB.csv` on a 16 MB part, leaving photos in a 1.5 MB `spiffs` entry. See the storage note below before changing that — but v3q is the moment to decide, because the freeze becomes real as soon as a v3q has photos on it.

## Hard constraints

Treat each of these as a hard failure if violated:

- **Sharp Memory LCD** (LS027B7DH01):
  - SCS is **active-high**, plain GPIO — not a normal SPI CS. Assert high to talk to it.
  - **Two bit orders on one wire.** Command and line-address bytes go **LSB-first** (`sendByte()` in `display.cpp` bit-reverses them). Pixel bytes go out **as they are, MSB-first** — MSB is the leftmost pixel, which matches the framebuffer. Sending pixels LSB-first too mirrors every 8-pixel group; that is exactly what the first v3q boot showed.
  - Mode 0. Datasheet max SCLK is 2 MHz; the firmware deliberately runs **8 MHz** — proven stable on this panel since the first prototype, and 2 MHz makes a frame push (~50 ms) laggy. If the panel glitches, drop the clock before suspecting anything else. `selftest` toggles 8 MHz / 2 MHz with `s` so a suspect board can be checked both ways.
  - **Write-only** — there is no MISO. Never attempt a read transaction.
  - DISP must be driven HIGH or the panel shows nothing.
- **The buzzer pin must idle LOW.** HIGH turns Q2 on, which puts near-DC through the coil. Sounds are bit-banged and always leave the pin LOW; `selftest` additionally calls `gpio_hold_en()` on it before sleeping. Do the same in the app when the pin map lands.
- **Nothing drives an unpowered sensor.** With `CAM_EN` low, every camera pin is an input. A driven pin into a dead rail leaks through the ESD diodes.
- **Shutter debounce lives in firmware.** Measured on v3q's predecessor: real presses hold ~200 ms, release bounce ~1 ms. A single 10 ms resample (`confirmPressed()` in main.cpp) covers that. Don't stack extra edge-timing windows on top of it.
- **Leave GPIO43 alone.** It idles high and is the flash-fob trigger on the dock pad.

## Firmware architecture

- **Camera** (`camera.{h,cpp}`): `PIXFORMAT_GRAYSCALE`, QVGA 320×240 (~77 KB/frame). Framebuffer lives in **internal DRAM, not PSRAM** — keep it that way unless there's a measured reason to change.
- **Dithering:** ordered **Bayer** for the live viewfinder (stable under motion, no shimmer), **Floyd–Steinberg** for frozen and stored frames (better static quality).
- **Display output:** source is 320×240, pillarboxed to the 400×240 panel.
- **UI mode machine** (`Mode` in main.cpp). One button drives everything, so what a press means depends entirely on the screen. **press = shoot**, **hold 700 ms (`GALLERY_HOLD_MS`) = gallery in/out**, **hold 1500 ms (`TRASH_HOLD_MS`) = trash** on the save screen, **hold 1000 ms (`WAKE_HOLD_MS`) = wake**. Every hold compares against a fresh `millis()` and fires *on the threshold*, not on release.
  - `Viewfinder` — live Bayer preview, `photos / hold` sidebar on the left. Press shoots.
  - `Capture` — the frozen frame, same layout. **Grab on press, commit on release:** the frame is captured and drawn on the press edge (zero shutter latency), but the dwell waits while the button is down. Released before `GALLERY_HOLD_MS` → a shot, then a `CAPTURE_DWELL_MS` hold and a slide to the save layout over `SAVE_SLIDE_MS`. Still down at `GALLERY_HOLD_MS` → the frame is dropped and the gallery opens. `Display::drawSaveTransition(t)` takes a linear 0–1 position; the ease-out cubic lives in main.cpp.
  - `Save` — the photo shifts flush left, send/trash takes the freed 80 px. Press = send, hold `TRASH_HOLD_MS` = trash. **Compare that hold against a fresh `millis()`, not the `now` sampled at the top of `loop()`** — `confirmPressed()` burns 10 ms, so `pressStartedAt` lands later than `now`, the unsigned subtraction wraps, and trash fires on every press. That bug shipped once already.
  - `Gallery` — one stored photo in the save geometry with `→ 3/12 / press` and `back / hold`. Press = next (newest→oldest, wraps), hold = back. No delete, no zoom; the phone does housekeeping. Both entering and leaving happen mid-hold, and both screens act only on a *press edge*, so the hold that opened the gallery never advances it. The screen is static: it repaints only when the photo changes or a toast appears/expires. The bitmap lives in `galleryBits` in main.cpp and `Display::drawGallery(bits, index, total)` never touches storage, which is what lets `tools/preview` render it.
  - `Asleep` — where the idle timeout leads (10 s; 30 s in the gallery and on save, `REVIEW_IDLE_TIMEOUT_MS`). The sleep face goes up and the camera comes down at once, but the chip stays awake advertising for `RADIO_LINGER_MS` (30 s), stretched while a phone is active (`Sync::activeRecently`) or the console is in use. Nothing is drawn but the face (it follows the mood and breathes every `BREATH_MS`) and a pairing code, which takes over the screen without waking the camera. Hold `WAKE_HOLD_MS` → viewfinder; a **tap chirps in the camera's current mood** and counts as nothing — it neither wakes nor stretches the radio window.
  - `Pairing` — the six-digit passkey, full screen, while the phone asks for it (`PAIRING_TIMEOUT_MS`, 60 s), then the outcome for `PAIRING_RESULT_MS` before handing the screen back where it came from. The button does nothing and the idle timeout is suspended. **The radio has no other UI**: connecting and transferring are the phone's business and raise no toast — which is also why the passkey is a screen, since the viewfinder's own hint used to paint straight over it.
  - **Toasts:** a timed toast only expires when something re-renders. Every mode transition calls `Display::clearToast()`, and the two static screens repaint when `Display::toastVisible()` changes. The viewfinder re-renders every frame and needs neither.
  - `Camera::capture()` hands back the driver's framebuffer and recycles it on the next call, so `Capture`/`Save` must never grab a new frame. Sleep frees it, hence the reset to `Viewfinder` in `enterSleepMode()`.
- **Photo storage** (`storage.{h,cpp}`): LittleFS on the partition table's 1.5 MB `spiffs` entry — that's a label, not a format; LittleFS survives power loss mid-write. Files are binary **PBM (P4)**: the packed 1-bit rows `Display::photoBits()` already holds, behind a short ASCII header, so they open in any viewer for free.
  - PBM sets a bit for **black**, the panel for **white**. Rows are inverted on write and on read.
  - The header carries one comment line, `# boot=17 up=48213 t=1757789000` (boot counter, `millis()` at capture, unix time or 0 if the clock was never set this boot). `Pbm::writeHeader()` / `parseHeader()` in `pbm_header.h`; spec in `docs/protocol.md` §2. Files from older firmware have no comment line and must keep parsing.
  - **Synced photos are renamed** `/0007.pbm` → `/0007s.pbm` when the phone ACKs them. That suffix is the only sync bookkeeping, and it lives with the file, so it survives everything. `photoAt(ordinal)` is newest-first.
  - **Eviction only touches synced photos.** When the partition is full, `savePhoto()` deletes the oldest `s` files until the new one fits; with none left it returns `Result::Full` and the review screen says `full, sync first`.
  - The index is a directory scan at boot, not an NVS counter, so it can't drift from what's on disk. The boot counter *is* in NVS (`Preferences("storage").boot`).
  - **Frozen for the sake of photos already on devices:** the mount call (`LittleFS.begin(true, "/littlefs", 10, "spiffs")`), the file naming and the P4 layout. Never `erase_flash`; `pio run -t upload` only rewrites the app slot and leaves photos alone. Back up with `tools/export/dump_flash.py` regardless. The **table itself** (`default_8MB.csv`) is the one piece still open, because v3q has 16 MB and no photos on it yet — a bigger photo partition keeping the `spiffs` label would leave `storage.cpp`, `dump_flash.py` and BLE sync untouched.
  - **Saving is 1-bit, so the greyscale is gone.** Intended: the blog shows the dithered frame exactly as the panel did.
- **Getting photos off:** `tools/export/dump_flash.py` reads the partition with esptool (any firmware). The USB console (`console.{h,cpp}`: `ls`, `get N`, `stat`; `docs/protocol.md` §6) with `tools/export/pull_serial.py`. And BLE sync, used by the iOS app.
- **Identity** (`identity.{h,cpp}`): camera id = Bluetooth MAC as 12 hex; short code derived from it (`shortcode.h`, vectors in `docs/protocol.md` §1); 16-byte secret minted once into NVS (`Preferences("sync").secret`). The secret is what the app presents to the server *as* the camera — only ever hand it out over an encrypted link.
- **Audio** (`audio.{h,cpp}`): three kinds of sound, two ways of making them. The **click** (shutter, wake, back to the viewfinder; a lighter tick into the gallery) and the **chirps** are bit-banged square waves (`digitalWrite` + `delayMicroseconds`, blocking) because the LEDC `tone()` path produced nothing for short sounds right after a light-sleep wake. The **melodies** (ta-da-da, its reverse for sleep, the chirp-chirp, the A-A'-E "saved") still use `tone()` from the async `Audio::update()` machine. Nothing is longer than 300 ms; ta-da-da is exactly that, and it is the ceiling.
  - **Chirps are composed, not listed:** `chirp.h` (Arduino-free) turns a happiness level 0-255 plus a seed into a 2-3 segment score — never a single beep, never under `MIN_TOTAL_MS` (100 ms) — of tones, sweeps and sample-and-hold noise: A major and rising when happy, detuned, off-scale, falling and noisy when sad. Every chirp is logged before it plays (`Sleep: chirp happiness=173/255 band=2 T1661/44 +12 S1661>2637/80 = 136ms`).
  - Audition on the host: `cd tools/chirp && make bands` renders the four bands and a 255→0 ladder to `out/*.wav` through the very same `Chirp::play()` loops the pin runs.
  - Chirps use `chirp.h`'s own xorshift so a score renders identically on the host; the click keeps Arduino's `random()`. Don't unify them.
- **Mood** (`mood.h`, glue at the top of main.cpp, NVS in `mood_store.{h,cpp}`, behaviour in `docs/mood.md`): happiness 255→0 over a week since the last photo. One chirp 55-65 min after a photo, then attention chirps from the day mark, the next drawn 12-20 h after the previous (so never more than two in 24 h), deferred to 08:00-10:00 local when they'd fall between 22:00 and 08:00 *and* the camera knows local time (SET_TIME carries a UTC offset; without one there is no night).
  - `mood.h` is a pure state machine over a `Mood::Clock` the caller builds; it never touches hardware. `serviceMood()` runs it every loop iteration and on every timer wake in light sleep, and only lets a chirp play on the viewfinder or the sleep face, with the button up, no melody and no transfer. **Never chirp on Save, Gallery or a slide.**
  - The clock is uptime from `esp_timer` (64-bit, runs through light sleep), resumed from NVS (`Preferences("mood").state`, 17 bytes, versioned) across resets. Once the phone has set the wall clock and the last photo's epoch is known, the epoch difference wins, so time powered off counts too; until then it does not.
  - NVS is written on a photo, a chirp, a clock change, and otherwise every 15 min (`Mood::persistDue`). Do not write it per tick.
  - The sleep face (`Display::drawSleep(happiness, breathIn, faceOnly)`) is 16 px block art drawn in code, one face per `Chirp::band`: the eyes carry the happiness (lifted corners only in the top band, flat below), the mouth carries the rest (a square until it stretches flat, then turns down). A full frame when it goes up or the band moves, face rows only for the breath every 5 s. Preview scenes `sleep`, `sleep-content`, `sleep-glum`, `sleep-sad`, `sleep-breath`.
  - `-DLC_MOOD_FAST` runs the mood clock 600× for bench tests (an hour in 6 s, the week in ~17 min). Off by default.
- **BLE sync** (`sync.{h,cpp}`, layout in `sync_protocol.h`, contract in `docs/protocol.md` §3): NimBLE-Arduino 2.x peripheral, one service, four characteristics (Info / Secret / Control / Data). The phone lists, pulls files as notification frames, and ACKs; an ACK renames the file to its `s` form.
  - **Nothing runs in NimBLE callbacks** except stashing the write and stamping `_lastBleActivity`. All storage and display work happens in `Sync::loop()` on the main thread; UI is raised through `Sync::nextEvent()` → toasts in main.cpp. Do not draw or touch LittleFS from the BLE task.
  - **Radio only while awake.** `Sync::end()` runs before `Camera::deinit()` in `enterSleepMode()`, `Sync::begin()` after the camera comes back. Light sleep with a live controller is not something this core supports; don't try to keep the link across sleep.
  - **When it runs:** while awake, and for 30 s behind the sleep face (`RADIO_LINGER_MS`), stretched while a connected phone has done something in the last 60 s (`BLE_ACTIVE_MS`). The shutter always wins; a transfer just continues alongside.
  - **Secret** is `READ_ENC | READ_AUTHEN` with passkey display — a random 6-digit code on the `Pairing` screen. If passkey pairing proves flaky on some phone, the fallback is Just Works (`READ_ENC` only, `BLE_HS_IO_NO_INPUT_OUTPUT`), which still stops passive readers.
  - Info is re-packed from `loop()` every 500 ms, never from the read callback, so its uptime is at most that stale.
  - `platformio.ini` disables the central/observer roles and caps connections at 1. NimBLE + camera fit the 3.3 MB app slot with room.
- **Sleep:** light sleep, entered once the radio window behind the sleep face closes, woken by the shutter or a `Display::VCOM_INTERVAL_MS` timer. The sleep loop leaves **only on a GPIO wake held for `WAKE_HOLD_MS`** (a tap chirps and goes straight back to sleep). A timer wake breathes the face and services the mood — a due chirp plays right there, radio and camera being down. Any other cause (BT, undefined, a rejected `esp_light_sleep_start()`) is logged and slept through: treating "anything but the timer" as a press made the camera wake by itself once the radio existed. `Sync::end()` forces the BT controller off and clears its wake source before sleeping, for the same reason.
  - The panel keeps showing the sleep face across light sleep (DISP stays HIGH). On v3q the panel's own VCOM comes from U8, so the timer wake is no longer what saves the pixels — see the migration notes.
  - `Camera::deinit()` before sleeping, `Camera::init()` after waking, at a cost of a few hundred ms on wake. On v3q this should drop the rails via `CAM_EN`, not just gate XCLK.
  - Beyond that, don't chase deep-sleep micro-optimizations at the cost of prototype complexity. There is no current sense, so every power figure here is an estimate.

## Build & flash

PlatformIO + Arduino framework, two projects: the app in `firmware/`, the board bring-up in `firmware/selftest/`.

```
pio run -e xiao_shutter                      # build the app (env rename pending)
pio run -e xiao_shutter -t upload -t monitor # flash and watch
pio device monitor                           # just watch
cd selftest && pio run -t upload -t monitor  # the board self-test
```

- `ARDUINO_USB_MODE=1` + `ARDUINO_USB_CDC_ON_BOOT=1`: **`Serial` is the native USB-CDC console**, not a UART bridge. The port re-enumerates after flashing, so the monitor may need a moment.
- `selftest` pins `espressif32 @ 8.0.0` (Arduino core 3.3.12) — the version it was checked against. The app floats; pin it too if a core bump breaks something.
- `-DLC_NO_BLE` builds without the radio, for bisecting.

## Host checks — no hardware needed

- `cd tools/hosttest && make test` runs the Arduino-free headers (PBM header codec, CRC-32, SHA-256, short code, chirp composer, a month of the mood machine, BLE frame layouts) against known vectors. `make check` syntax-compiles audio/mood_store/storage/console/identity/sync/main against stubbed Arduino headers. Neither replaces `pio run`, but both run anywhere.
- `tools/preview/` compiles the **real** `src/display.cpp` for the host against a stub Arduino/SPI layer and writes what the panel would show to a BMP. **Iterate on layout here before flashing** — once the app starts light-sleeping, every upload needs a physical shutter press to catch the board awake. Host toolchain only (`c++`, no deps), and nothing under `src/` is modified or conditionally compiled for it.

```
cd tools/preview && make scenes   # render every scene to out/ (make open on macOS)

./preview <scene> [options]        # --help lists every scene and flag
scenes: viewfinder capture save slide toast splash
        sleep sleep-content sleep-glum sleep-sad sleep-breath
        pairing pairing-done
        gallery gallery-slide gallery-empty
  --out PATH / --scale N      output BMP, integer upscale (default 2)
  --src FILE / --pbm FILE     320x240 binary PGM source, or a PBM photo
  --at MS / --t F             virtual clock, slide position 0-1
  --happiness N / --breath    sleep face mood 0-255, second breath frame
  --index N / --total N       gallery counter
```

How far to trust it: it intercepts at the **SPI byte stream**, not the framebuffer, so it exercises the real wire protocol — command bits, VCOM bit, line addressing, bit order. That's why `drawSplash()` previews correctly even though it streams PROGMEM straight to SPI. `millis()` is a virtual clock, so toast expiry and animation are reproducible frame by frame. It warns on **DISP low** and on **SPI protocol errors**, two of this panel's classic blank-screen causes. It models the **digital side only** — no contrast, no 8 MHz behaviour, no refresh timing, no ghosting. Layout and dithering are faithful; "will the panel like this" is still a hardware question. Add a scene by adding a branch in `preview.cpp`, and widen the Arduino shim when a build fails rather than emulating Arduino wholesale.

## Open tasks / known unknowns

Flag these if relevant to a change; don't silently assume they're resolved.

- **`src/` has never run on v3q.** The pin map, the shutter polarity, the camera rails and the env name are all still the old board's. See the migration section — this is the next job.
- **Mood is untested on hardware.** Host-tested over a simulated month and previewed, but the first real week — NVS resume, chirps out of light sleep, the breath's power cost (a few hundred µA average, unmeasured) — is still ahead. `-DLC_MOOD_FAST` compresses it to a bench session.
- **BLE sync is untested on hardware.** Written against NimBLE 2.x, checked on the host only. First things to verify with a board: the `pio run` size line, pairing with the passkey screen, an end-to-end pull from the app, and that the stack comes back cleanly after a light-sleep cycle.
- **Firmware-over-BLE** lives on the open `claude/firmware-update-ble-gvwlz5` branch, unmerged. It matters more on v3q than it did on the carrier: no castellated pads, no exposed USB once there's an enclosure.
- **No modem.** The retired SIM7080G integration (AT+CBC, AT+CPOWD) went with the LilyGO board and has no home. The phone is the uplink until it does.
- **The 16 MB part is half unused** — see the storage note. Decide before v3q boards carry photos.
- **Enclosure** is unresolved.
- Cosmetic boot logs: `spiAttachMISO(): HSPI Does not have default pins on ESP32S3!` (we pass -1; the panel is write-only) and a one-shot `ledc_get_duty(): LEDC is not initialized` from the `tone()` HAL. Both harmless.

## Conventions

- One-line comments only, directly above what they explain, and only for a non-obvious *why*. If it needs a paragraph, the code needs a better name.
- Justify non-obvious decisions with the tradeoff, matching how the rest of the project is documented.
- Prefer the simplest thing that works on real hardware. If a change adds complexity, say what it buys and what it costs.
