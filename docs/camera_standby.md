# Camera standby & sleep current — findings

Research notes on whether the OV2640 can be powered down or put to sleep on this hardware.

**Verdict: it cannot, in firmware. Don't try.** This document exists so nobody re-litigates it.

---

## TL;DR

Two obvious-looking approaches get suggested constantly. Both are wrong on this board:

| Approach | What it costs | What it buys |
|---|---|---|
| `esp_camera_deinit()` before sleep | a few hundred ms re-init on every wake | **0 mA** |
| OV2640 software standby (COM2 bit 4) | **wedges the I²C bus until physical power removal** | **0 mA** |

The camera's ~20 mA is ungateable because the Seeed Sense expansion board hard-wires it on. That's a copper problem, not a code problem. The fix, if we ever need one, is a board mod — see [What would actually work](#what-would-actually-work).

Current stance: **change nothing.** This matches `AGENTS.md` → *"Sleep: don't chase deep-sleep micro-optimizations at the cost of prototype complexity."*

---

## Why anyone cares

We use light sleep with a 10 s idle timeout. The camera draws roughly 20 mA whether or not we're using it, so sleep only removes the ESP32's share:

| State | Rough draw |
|---|---|
| Awake, viewfinder running | ~50 mA |
| Light sleep (current firmware) | **~23 mA** — ESP32 ~3 mA + camera ~20 mA |
| Light sleep, if the camera were gateable | ~3 mA |

So sleep buys about **2×**, not the ~10× you'd expect. On a 500 mAh cell that's roughly a day of idle versus a week.

That gap is the entire prize, and it is only reachable through hardware.

> **Not yet measured:** these are community figures for deep sleep on the Sense board. Nobody has published a *light* sleep measurement taken after `esp_camera_init()`, and we haven't taken one either. See [Open question](#open-question-the-one-measurement-we-still-need).

---

## Approach 1: `esp_camera_deinit()` — does nothing

The intuition is that tearing the driver down should release the sensor. It doesn't. Here is the entire function (`driver/esp_camera.c`, unchanged since v2.0.4):

```c
esp_err_t esp_camera_deinit() {
    esp_err_t ret = cam_deinit();
    CAMERA_DISABLE_OUT_CLOCK();
    if (s_state) { SCCB_Deinit(); free(s_state); s_state = NULL; }
    return ret;
}
```

Three problems:

1. **It never touches PWDN or RESET.** Those pins are referenced only inside `camera_probe()` at init. The sensor stays powered, in whatever register state it was left in.
2. **`CAMERA_DISABLE_OUT_CLOCK()` is a no-op on ESP32-S3.** On the original ESP32 the camera clock (XCLK) comes from LEDC and genuinely gets stopped. On the S3 it comes from the LCD_CAM peripheral, and the driver simply doesn't implement a stop path — the macro compiles to nothing.
3. **It deletes the I²C driver**, so any register-level trick has to happen *before* this call, not after.

### Verified against our own toolchain

This isn't just true of upstream `master`. Symbol dump of the prebuilt `libesp32-camera.a` we actually link (`framework-arduinoespressif32` **3.20017**, Arduino core 2.0.17):

```
=== esp32 ===                        === esp32s3 ===
     U camera_disable_out_clock      (absent entirely)
     U camera_enable_out_clock       (absent entirely)
00000000 T camera_disable_out_clock
00000000 T camera_enable_out_clock
```

On the ESP32 build the symbol is both defined (`T`) and referenced (`U`) — `esp_camera_deinit()` really calls it. On the **ESP32-S3 build it doesn't exist at all**. Our `esp_camera_deinit()` provably cannot stop XCLK.

The header even admits the general situation (`esp_camera.h:188`): *"no way to de-initialize this module."*

Multiple independent reporters measured the real-world result: deinit before sleep "makes no difference," still ~20 mA.

---

## Approach 2: COM2 standby — works once, then bricks the bus

### Background: SCCB and register banks

The OV2640 is configured over **SCCB**, OmniVision's I²C-compatible bus (`SIOD`/`SIOC` = GPIO40/39 here). It has two register banks that share an address space; register `0xFF` bit 0 selects which one. Bank 0 is the DSP block, bank 1 is the sensor block.

In the esp32-camera driver, `sensor->set_reg()` encodes the bank in bit 8 of the address. So sensor-bank register `0x09` is addressed as `0x109`.

### The register is real

OV2640 datasheet v1.6, Table 13, p.22 — sensor bank:

```
09  COM2  Bit[4]: Standby mode enable   0: Normal  1: Standby
```

The driver even defines the constant (`ov2640_regs.h:160`, `#define COM2_STDBY 0x10`) — and references it **zero times**. It's dead code inherited from OpenMV. There is no sleep/standby/power hook anywhere in the `sensor_t` function table.

### The failure

This circulates as a working snippet ([esp32-camera#672](https://github.com/espressif/esp32-camera/issues/672)):

```c
// i=1: sleep, i=0: wake
void camera_deep_sleep(int i) {
  sensor_t *s = esp_camera_sensor_get();
  s->set_reg(s, 0x109, 0x10, i ? 0x10 : 0);
}
```

The author's own follow-up: *"let me know if you find any way to wake it back up :)"*

Nobody ever has. Thread reply, same day: *"Putting it to sleep is not the issue. Getting it to wake up again is where we all get stuck. It appears to be a hardware design flaw. The only workaround is to cut power and restart."*

### Why it can't be woken

> **Inference, not documented.** This is our reconstruction, but it predicts every reported symptom.

An SCCB write is `START · slave_addr · 0x09 · data · ACK`. The slave pulls SDA low to acknowledge the final data byte, then releases it. But the byte it just latched is the one that gates its own clock domain — so the sensor enters standby *while still asserting the ACK pull-down*, and the logic that would release SDA is now unclocked. SDA stays low forever.

Every reported symptom follows:

- One reporter logged `SCCB_Write Failed addr:0x30, reg:0x09, data:0x12, ret:263`. `263` is `ESP_ERR_TIMEOUT` — **the write times out yet the sensor still enters standby.** Exactly what you'd see if the command landed but the ACK never completed.
- Standard I²C bus recovery (clock out 9 SCL pulses to force the slave to release SDA) was tried and **failed**. It relies on the slave's shift register advancing; an unclocked slave ignores SCL entirely.
- Stopping and restarting XCLK before the wake write was tried and **failed**. The internal clock gate is downstream of XCLK.
- It takes down the **whole bus**, not just the camera, because SDA is shared.

Every escape route fails for the same reason: they all require the sensor to execute something, and it can't execute anything.

### The datasheet is wrong here

DS v1.6 p.6 claims software power-down *"suspends internal circuit activity but **does not halt the device clock**"* and that registers are retained. If that were true, the SCCB slave would stay alive and the wake write would work. Observed behaviour says the clock **does** halt. Don't design against that sentence.

OmniVision's own Hardware Application Note §3.1 quietly concedes the broader point:

> "For OV2640, there have big power down current, so **power down mode is not supported by OV2640 for power saving.**"

### It wouldn't even save power here

Measured on a XIAO ESP32S3 Sense, same rig, three sensors:

| Sensor | Deep sleep, no standby | With register standby |
|---|---|---|
| **OV2640 (ours)** | **22.3 mA** | **22.3 mA** |
| OV3660 | 37.8 mA | 1.45 mA |
| OV5640 | 104 mA | 1.4 mA |

OV3660/OV5640 use register `0x3008` bit 6, which doesn't exist on the OV2640.

### Why it's worse for us than for the people in that thread

They say the workaround is "cut power and restart." On a typical ESP32-CAM that means a reboot or a power-cycled module — annoying but recoverable.

**On this board there is no way to cut camera power.** `esp_restart()` won't clear it. A watchdog reset won't clear it. Re-flashing won't clear it. The only recovery is **physically unplugging USB and the battery.**

So the trade is: risk a state the firmware cannot recover from, for 0 mA.

---

## The hardware blocks

Decoded from the [XIAO ESP32S3 Expansion Board v1.0 schematic](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/XIAO_ESP32S3_ExpBoard_v1.0_SCH.pdf). Seeed doesn't link this from the wiki; it surfaced in a forum thread.

Three independent reasons firmware can't win:

1. **PWDN and RESET are strapped, not routed.** Camera FPC pin 8 (PWDN) goes to **R10, 10 kΩ to GND**; pin 6 (RESET) goes to **R9, 10 kΩ to 2.8 V**. Neither reaches a GPIO or the B2B connector. This is why `PWDN_GPIO_NUM = -1` and `RESET_GPIO_NUM = -1` in every XIAO pin map — it's a hardware fact, not a driver oversight. There is no pin to assign.

2. **Both camera LDOs are permanently enabled.** U1 (SGM2036-2.8 V) and U2 (SGM2036-1.3 V) each have their **EN pin tied directly to their own IN pin**. Enabled by copper. No GPIO, no pull network, nothing in between.

3. **They're fed from VIN**, which is diode-OR'd from VBUS/battery *upstream of the ESP32-S3 entirely*. There is no firmware-reachable point in that path.

For completeness: the two LDOs' own quiescent draw is only ~20 µA each. **The LDOs aren't the problem — the OV2640 they keep alive is.** (A widely-circulated blog post claims the LDOs are "90% of the draw" and that cutting the XIAO's regulator trace fixes it. That's wrong by two orders of magnitude and conflates the bare-XIAO mod with the Sense camera rails. Ignore it.)

---

## What would actually work

All hardware. Ranked by effort.

| Lever | Result | Reversible | Cost |
|---|---|---|---|
| Manually park XCLK (GPIO10) before sleep | 64→44 mA on ESP32-CAM; **unproven on S3** | yes | ~5 lines, cheap experiment |
| Remove **R14** + external load switch | kills all camera power | yes | SMD rework, full re-init on wake |
| External RTC / load switch on the battery line | **2 µA** measured | yes | extra hardware |
| Swap OV2640 → OV3660 | 22.3 → 1.45 mA | yes | new sensor + `0x3008` standby |

**R14 is the interesting one.** It's a 0 Ω resistor and the *single* series element between VIN and `VCC_IN`, the shared input of **both** camera LDOs. One 0402 removal kills camera power cleanly, and `VCC_IN` is exposed at test point **TP1** to re-feed from a GPIO-controlled load switch.

This doesn't appear in any forum thread — people were proposing interposer PCBs and soldering to 0.5 mm FPC pins instead. Caveats: camera inrush is significant, so size the switch accordingly, and budget a full `esp_camera_init()` on every wake.

---

## Open question: the one measurement we still need

Everything above is about **deep** sleep. We use **light** sleep, and no published measurement exists for this board post-`esp_camera_init()`. Whether LCD_CAM's XCLK survives `esp_light_sleep_start()` is undocumented and shouldn't be guessed at.

Three readings, USB power meter or meter in the battery line:

1. Boot, never call `Camera::init()`, straight to light sleep
2. Normal boot with camera init, then light sleep
3. Awake, viewfinder running

The gap between 1 and 2 is the camera's light-sleep cost.

**Decision rule on the result:**

- **< 5 mA** → done. Close the topic, nothing left worth doing.
- **~20 mA** → try parking XCLK manually before `esp_light_sleep_start()` (detach the GPIO matrix from GPIO10, drive it low; restore on wake). Fully reversible; worst case we re-init the camera.
- **Still ~20 mA after that** → it's the hardware. Defer until the battery path is decided (`AGENTS.md` lists it as open), then do the R14 mod on the next carrier spin rather than scraping a resistor off a Seeed board.

---

## Verified vs. inferred

| Claim | Status |
|---|---|
| `esp_camera_deinit()` never touches PWDN/RESET | **Verified** — driver source, all tags |
| `CAMERA_DISABLE_OUT_CLOCK()` is a no-op on ESP32-S3 | **Verified** — source *and* symbol dump of our own `.a` |
| No sleep/standby/power hook in `sensor_t` | **Verified** — our installed `sensor.h` |
| COM2 = `0x09`, bit 4 = standby, sensor bank → `set_reg(s, 0x109, 0x10, v)` | **Verified** — DS v1.6 p.22 + driver source |
| `COM2_STDBY` defined but never used by the driver | **Verified** — repo-wide grep |
| PWDN strapped low via R10; LDO EN tied to IN; LDOs fed from VIN | **Verified** — Seeed schematic v1.0 |
| OV2640 standby: 22.3 → 22.3 mA on this board | **Measured** — community, single rig |
| COM2 standby wedges SCCB SDA until power cycle | **Strongly corroborated** — multiple independent reporters, never refuted, never fixed |
| *Mechanism*: sensor halts mid-ACK, leaving SDA asserted | **Inference** — ours; explains every symptom but is not documented |
| OmniVision: "power down mode is not supported by OV2640 for power saving" | **Verified** — HW App Note §3.1 |
| DS p.6 "<15 µA" hardware power-down figure | **Present but unreliable** — contradicts Table 6 (600 µA typ / 1200 µA max) |
| XCLK parking helps on ESP32-S3 | **Unverified** — only measured on ESP32-CAM |

---

## Sources

Driver and datasheets:
- [espressif/esp32-camera — `esp_camera.c`](https://github.com/espressif/esp32-camera/blob/master/driver/esp_camera.c) · [`ov2640_regs.h`](https://github.com/espressif/esp32-camera/blob/master/sensors/private_include/ov2640_regs.h) · [`esp32s3/ll_cam.c`](https://github.com/espressif/esp32-camera/blob/master/target/esp32s3/ll_cam.c)
- [OV2640 Datasheet v1.6](https://www.uctronics.com/download/cam_module/OV2640DS.pdf)
- [OV2640 Camera Module Hardware Application Notes](https://hobbylad.wordpress.com/wp-content/uploads/2020/02/ov2640-camera-module-harware-application-notes.pdf)
- [XIAO ESP32S3 Expansion Board v1.0 schematic](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/XIAO_ESP32S3_ExpBoard_v1.0_SCH.pdf)

Issue threads:
- [esp32-camera#101 — OV2640 standby support](https://github.com/espressif/esp32-camera/issues/101) (2019, unresolved)
- [esp32-camera#672 — revisit](https://github.com/espressif/esp32-camera/issues/672) (2024, still unresolved)
- [esp32-camera#33 — power down modes](https://github.com/espressif/esp32-camera/issues/33)
- [Seeed forum — XIAO ESP32S3 Sense camera sleep current](https://forum.seeedstudio.com/t/xiao-esp32s3-sense-camera-sleep-current/271258)
- [Seeed forum — very high deep sleep current](https://forum.seeedstudio.com/t/esp32s3-sense-camera-very-high-deep-sleep-current-any-solution/274705)
- [Mjrovai/XIAO-ESP32S3-Sense#1](https://github.com/Mjrovai/XIAO-ESP32S3-Sense/issues/1)

Espressif's position, [#522](https://github.com/espressif/esp32-camera/issues/522): *"for devices with cameras, the main power consumption is concentrated in the camera sensor and RF module."* Requests for a sleep API date to 2019 and have never been implemented.
