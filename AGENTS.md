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
- **UI mode machine** (`Mode` in main.cpp). One button drives everything, so what a press means depends entirely on the screen:
  - `Viewfinder` — live Bayer preview, sidebar on the left. Press shoots.
  - `Capture` — frozen Floyd–Steinberg frame, same layout. Holds for `CAPTURE_DWELL_MS`, then slides to the save layout over `SAVE_SLIDE_MS` (ease-out cubic, driven from main.cpp — `Display::drawSaveTransition(t)` takes a linear 0–1 position and knows nothing about the curve).
  - `Save` — photo shifts flush left, send/trash column takes the freed 80px on the right. Press = send, hold `TRASH_HOLD_MS` (800ms) = trash. The hold fires *on the threshold*, not on release, so it has an end you can feel.
  - Not a gallery. `Display::drawSave()` only ever acts on the frame you just shot; browsing stored photos is a separate future screen.
  - Sending has no destination on this carrier revision (no modem) — the send path is UI only.
  - `Camera::capture()` hands back the driver's framebuffer and recycles it on the next call, so `Capture`/`Save` must never grab a new frame. Sleep frees it, hence the reset to `Viewfinder` in `enterSleepMode()`.
- **Sleep:** light sleep, woken by the shutter (HIGH level) or a `Display::VCOM_INTERVAL_MS` timer. Two things are load-bearing — don't remove them:
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

scenes:   viewfinder  capture  save  slide  toast  sleep  splash
  --out PATH   output BMP            (default preview.bmp)
  --scale N    integer upscale       (default 2 — 1:1 is unreadable on hidpi)
  --src FILE   320x240 binary PGM    (default: built-in synthetic test image)
  --at MS      virtual clock value   (default 0)
  --t F        slide position 0-1     (default 0.5, 'slide' scene only)
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

- **Buzzer is dead in hardware** — firmware output on GPIO2 is verified (bit-bang square wave produced no sound; continuity check pointed at the BZ1 net/module). Fix the solder joint / module before trusting audio feedback.
- **Cellular/modem** is not on this carrier revision — the previous SIM7080G integration (AT+CBC battery readout, AT+CPOWD power-down) is retired with the LilyGO board and needs a new home.
- **Battery/power path** for the carrier (XIAO BAT pads vs. external charger) is undecided.
- **Enclosure** is unresolved for the new board.
- Cosmetic boot logs: `spiAttachMISO(): HSPI Does not have default pins on ESP32S3!` (we pass -1 for MISO — panel is write-only) and a one-shot `ledc_get_duty(): LEDC is not initialized` from the tone() HAL. Both harmless.

## Conventions

- Justify non-obvious decisions in a comment with the tradeoff, matching how the rest of the project is documented.
- Prefer the simplest thing that works on real hardware. If a change adds complexity, say what it buys and what it costs.
