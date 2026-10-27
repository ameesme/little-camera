# Camera standby & sleep current

**Settled: the camera cannot be powered down from firmware, and on v3q it does not have to be.** This file exists so nobody re-litigates the two firmware routes, which are dead ends on any board.

## Where v3q landed

`CAM_EN` (GPIO45) enables U4/U5/U6, and `CAM_PWDN` and `CAM_RESET` go to real pins instead of being strapped. `Camera::deinit()` drops the rails, so the sensor loses power rather than just its clock. Measured on board 1:

| State | Old carrier | v3q board 1 |
|---|---|---|
| Awake, camera on | ~50 mA | 105 mA |
| Awake, camera off | — | 62 mA |
| Light sleep | **~23 mA** | **~4 mA** |

Sleep is worth about 15× now instead of 2×. None of the remaining 4 mA is the sensor: roughly 2 mA is U8, the display's VCOM oscillator, confirmed by bridging C18, and roughly 2 mA is the S3 itself. That is about 3 days asleep on a 300 mAh cell. The next improvement is a micropower oscillator on the next board revision, which is not something firmware can do.

**Debugging rule:** if light sleep ever reads ~20 mA again, `CAM_EN` is not actually cutting the rails. Check that `Camera::deinit()` runs before `esp_light_sleep_start()`, and that nothing is still driving a camera pin into a dead rail and leaking through its ESD diodes.

## The two firmware routes, both dead

| Approach | What it costs | What it buys |
|---|---|---|
| `esp_camera_deinit()` before sleep | a few hundred ms re-init on every wake | **0 mA** |
| OV2640 software standby (COM2 bit 4) | **wedges the I²C bus until power is physically removed** | **0 mA** |

Both are properties of the driver and the sensor rather than of any board, so they are still dead on v3q.

### `esp_camera_deinit()` does nothing

It never touches PWDN or RESET — those pins are only referenced inside `camera_probe()` at init — so the sensor stays powered in whatever register state it was left in. `CAMERA_DISABLE_OUT_CLOCK()` is a no-op on the ESP32-S3: on the original ESP32 the camera clock comes from LEDC and really does stop, but on the S3 it comes from LCD_CAM and the driver has no stop path. A symbol dump of the prebuilt `libesp32-camera.a` we link (`framework-arduinoespressif32` 3.20017) shows `camera_disable_out_clock` defined and referenced in the esp32 build and **absent entirely from the esp32s3 build**. The header admits it too (`esp_camera.h:188`): *"no way to de-initialize this module."* It also deletes the I²C driver, so any register-level trick has to happen before the call.

### COM2 standby works once, then bricks the bus

The register is real — OV2640 DS v1.6 Table 13 p.22, sensor bank `09 COM2` bit 4, addressed as `set_reg(s, 0x109, 0x10, v)` — and the driver defines `COM2_STDBY` and never uses it. Writing it puts the sensor to sleep and nobody has ever woken one up ([esp32-camera#672](https://github.com/espressif/esp32-camera/issues/672), and #101 since 2019). The likely mechanism, ours and undocumented: the sensor latches the bit that gates its own clock domain while it is still asserting the ACK pull-down, so SDA stays low forever and the whole shared bus goes with it. That predicts every reported symptom — the write returning `ESP_ERR_TIMEOUT` yet the sensor still entering standby, nine-SCL-pulse bus recovery failing, restarting XCLK failing.

The datasheet claims software power-down *"does not halt the device clock"* and that registers are retained (p.6). Observed behaviour says otherwise, so don't design against that sentence. OmniVision's own Hardware Application Note §3.1 concedes the point: *"power down mode is not supported by OV2640 for power saving."*

It would not even save anything here. Measured on a XIAO ESP32S3 Sense, same rig:

| Sensor | Deep sleep, no standby | With register standby |
|---|---|---|
| **OV2640 (ours)** | **22.3 mA** | **22.3 mA** |
| OV3660 | 37.8 mA | 1.45 mA |
| OV5640 | 104 mA | 1.4 mA |

OV3660 and OV5640 use register `0x3008` bit 6, which the OV2640 does not have.

## Why the old carrier could not be fixed in firmware

Kept in one paragraph, because it is the argument that produced `CAM_EN`. On the retired XIAO Sense expansion board, PWDN and RESET were strapped through 10 kΩ rather than routed (hence `PWDN_GPIO_NUM = -1` in every XIAO pin map), both camera LDOs had EN tied to their own IN, and they were fed from VIN upstream of the ESP32-S3. Three independent copper reasons, no firmware-reachable point in the path. The LDOs' own quiescent draw was ~20 µA each: the problem was the OV2640 they kept alive, not the regulators.

## Verified vs inferred

| Claim | Status |
|---|---|
| `esp_camera_deinit()` never touches PWDN/RESET | **Verified** — driver source, all tags |
| `CAMERA_DISABLE_OUT_CLOCK()` is a no-op on ESP32-S3 | **Verified** — source and a symbol dump of our own `.a` |
| No sleep/standby/power hook in `sensor_t` | **Verified** — our installed `sensor.h` |
| COM2 = `0x09`, bit 4 = standby, sensor bank | **Verified** — DS v1.6 p.22 + driver source |
| OV2640 standby: 22.3 → 22.3 mA | **Measured** — community, single rig |
| COM2 standby wedges SCCB SDA until a power cycle | **Strongly corroborated** — multiple reporters, never refuted |
| *Mechanism*: the sensor halts mid-ACK, leaving SDA asserted | **Inference** — ours, explains every symptom, not documented |
| Gating the camera rails removes it from the sleep budget | **Measured** — v3q board 1, ~23 → ~4 mA |

## Sources

- [espressif/esp32-camera — `esp_camera.c`](https://github.com/espressif/esp32-camera/blob/master/driver/esp_camera.c) · [`ov2640_regs.h`](https://github.com/espressif/esp32-camera/blob/master/sensors/private_include/ov2640_regs.h) · [`esp32s3/ll_cam.c`](https://github.com/espressif/esp32-camera/blob/master/target/esp32s3/ll_cam.c)
- [OV2640 Datasheet v1.6](https://www.uctronics.com/download/cam_module/OV2640DS.pdf) · [Hardware Application Notes](https://hobbylad.wordpress.com/wp-content/uploads/2020/02/ov2640-camera-module-harware-application-notes.pdf)
- [XIAO ESP32S3 Expansion Board v1.0 schematic](https://files.seeedstudio.com/wiki/SeeedStudio-XIAO-ESP32S3/res/XIAO_ESP32S3_ExpBoard_v1.0_SCH.pdf)
- Issues [#672](https://github.com/espressif/esp32-camera/issues/672), [#101](https://github.com/espressif/esp32-camera/issues/101), [#33](https://github.com/espressif/esp32-camera/issues/33), [#522](https://github.com/espressif/esp32-camera/issues/522) · [Seeed forum: sleep current](https://forum.seeedstudio.com/t/xiao-esp32s3-sense-camera-sleep-current/271258) · [Seeed forum: deep sleep current](https://forum.seeedstudio.com/t/esp32s3-sense-camera-very-high-deep-sleep-current-any-solution/274705)
