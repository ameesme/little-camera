# CLAUDE.md

Project context for Claude Code. Read this before touching firmware, pin config, or hardware-facing code. The constraints below are load-bearing — several were derived by metering the actual board and violating them either bricks a peripheral or silently breaks the display.

## Project

Standalone, battery-powered cellular IoT camera. It captures a small number of low-resolution grayscale photos per day, uploads them over 4G LTE-M, periodically polls for incoming images, shows a live grayscale viewfinder plus received images on a monochrome Sharp Memory LCD, and gives audio + button feedback. This is a hardware prototype; firmware runs on an ESP32-S3.

Guiding philosophy: pragmatic and buildable over theoretically optimal. Tradeoffs are made explicitly and documented. Push back on over-engineering rather than adding complexity by default.

## Hardware stack

- **Main board:** LilyGO S3_CAM_SIM v1.2 — ESP32-S3R8 (8MB PSRAM variant) with OV2640 camera
- **Modem:** LilyGO T-PCIE SIM7080G in the mPCIe slot — UART on GPIO45 (TX) / GPIO46 (RX), power/reset on GPIO48
- **Display:** Adafruit 4694 Sharp Memory LCD (LS027B7DH01), 2.7", 400×240, monochrome
- **Audio:** Keyestudio passive buzzer module
- **Input:** single micro-switch button
- **Power:** 500mAh LiPo on JST connector; charging via onboard TP4056
- **SD card:** deliberately removed from the design — its SPI lines are repurposed (see below)

## Pin allocation — DO NOT REASSIGN

These are final. Every free GPIO on this board was hard-won; do not "optimize" the map or move a function without an explicit request.

| Function | GPIO | Notes |
|---|---|---|
| Display SCLK | GPIO39 | ex-SD CLK |
| Display MOSI | GPIO38 | ex-SD CMD |
| Display CS | GPIO40 | ex-SD DAT0/MISO |
| Buzzer SIG | GPIO47 | ex-SD DAT3/CS |
| Button | GPIO21 | ex-modem LED, on 6-pin header |
| Modem UART TX | GPIO45 | to SIM7080G |
| Modem UART RX | GPIO46 | from SIM7080G |
| Modem PWR/RST | GPIO48 | |
| Debug console | GPIO19 / GPIO20 | USB-CDC |

### microSD socket pin repurposing

The onboard microSD socket is not used; its pins are repurposed for the display and buzzer.

| microSD pin | SD-SPI name | Board GPIO | Your use |
|---|---|---|---|
| 1 | DAT2 | — | not connected — leave floating |
| 2 | DAT3 / CS | GPIO47 | Buzzer SIG |
| 3 | CMD / MOSI (DI) | GPIO38 | Display MOSI (DI) |
| 4 | VDD | 3V3 | Display VIN + Buzzer VCC |
| 5 | CLK / SCLK | GPIO39 | Display SCLK |
| 6 | VSS | GND | Display GND + Buzzer GND |
| 7 | DAT0 / MISO (DO) | GPIO40 | Display CS |
| 8 | DAT1 | — | not connected — leave floating |

## Hard constraints

Treat each of these as a hard failure if violated:

- **Never use GPIO31.** It is hardwired to flash SPIQ on the R8 module. Using it corrupts flash access.
- **Never initialize the SD / SDMMC driver.** The SD peripheral shares GPIO38/39/40/47 with the display and buzzer. Any `sdmmc`/`sdspi`/SD_MMC init reclaims those pins and silently breaks the LCD and buzzer. No SD init in any code path, including debug or examples.
- **GPIO0 is a BOOT strap** — that's why the button lives on GPIO21. Do not move the button onto GPIO0 or add any drive to GPIO0.
- **Sharp Memory LCD specifics** (LS027B7DH01):
  - CS is **active-high**, plain GPIO — not a normal SPI CS. Assert high to talk to it.
  - SPI is **LSB-first, mode 0**.
  - **Write-only** — there is no MISO. Never attempt a read transaction.
  - VCOM is handled in **software**; EXTMD is tied low, DISP is tied high. Firmware must toggle VCOM periodically to prevent DC bias / image burn-in.
- **Battery monitoring is via `AT+CBC` over the modem UART.** There is no ADC pin for battery voltage — do not add or assume one.
- **Modem power management uses `AT+CPOWD`** to power the modem down between sessions. Do not depend on carrier PSM; PSM support is unreliable across carriers and was deliberately designed out.

## Firmware architecture

- **Camera:** `PIXFORMAT_GRAYSCALE`, QVGA 320×240 (~77KB/frame). Framebuffer lives in **internal DRAM, not PSRAM** — keep it that way unless there's a measured reason to change.
- **Live viewfinder:** ordered **Bayer** dithering — stable and fast under motion, no shimmer.
- **Received / stored images:** **Floyd–Steinberg** dithering for better static-image quality.
- **Display output:** source is 320×240; pillarbox to the 400×240 panel.
- **Sleep floor:** accept ~1–2mA board leakage in sleep rather than adding custom power electronics. Estimated runtime is several days on the 500mAh cell. Don't chase deep-sleep micro-optimizations at the cost of prototype complexity.

## Build & flash

PlatformIO + Arduino framework. Environment is `s3camsim`.

- **Build:** `pio run -e s3camsim`
- **Upload:** `pio run -e s3camsim -t upload`
- **Monitor:** `pio device monitor`
- **Build + upload + monitor:** `pio run -e s3camsim -t upload -t monitor`

Config facts that affect firmware:

- Board `esp32-s3-devkitc-1`, MCU esp32s3, **16MB flash**, partitions `default_16MB.csv`.
- Memory type `qio_opi` — **QIO flash + OPI PSRAM**. PSRAM is present (`BOARD_HAS_PSRAM`), but per the architecture note the camera framebuffer stays in internal DRAM regardless.
- `ARDUINO_USB_MODE=1` + `ARDUINO_USB_CDC_ON_BOOT=1`: **`Serial` is the native USB-CDC console** (the GPIO19/20 debug console), not a UART bridge. The USB port re-enumerates after flashing, so the monitor may need a moment to reconnect.
- The **modem uses a separate hardware UART** on GPIO45/46 — instantiate it explicitly and keep it distinct from `Serial`.

## Open tasks / known unknowns

Flag these if relevant to a change; don't silently assume they're resolved:

- **JST polarity must be metered before the first battery connection** — not yet verified.
- **TP4056 charge current** may be marginal on weak USB sources — open concern.
- **Antenna placement** is unresolved and blocks closing the enclosure.
- Enclosure (~92×55×19.6mm): camera lens should move to a corner (off the center-back grip), buzzer needs a sound port, and the camera flex-cable bend radius is currently too tight.

## Conventions

- Justify non-obvious decisions in a comment with the tradeoff, matching how the rest of the project is documented.
- Prefer the simplest thing that works on real hardware. If a change adds complexity, say what it buys and what it costs.