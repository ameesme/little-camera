# Little Camera — hardware

The board the firmware runs on. KiCad 10, four copper layers, 80 parts, 58 nets. Current revision **v3q**.

```
pcb/little_camera.kicad_sch     schematic
pcb/little_camera.kicad_pcb     board
pcb/little_camera_2d_view.svg   the board as a picture, no KiCad needed
pcb/gen/                        the Python that writes the board
pcb/fab/gerbers/                what was sent to the fab
pcb/out/*.md                    verification and audit records (the rest of out/ is generated)
```

The board is not drawn by hand. `pcb/gen/design.py` is the one place that holds the parts and every connection, `pcb/gen/v3build.py` cuts the bare frame and stackup, `pcb/gen/v3place.py` places, and routing goes out as a `.dsn` to freerouting and comes back as a `.ses` (`pcb/gen/route/`, not tracked). Open pcbnew and drag a track and the script and the board stop agreeing, so change the script and regenerate instead.

Pin assignments are the contract with the firmware. They live in `firmware/AGENTS.md` and have to change in both places at once.

Read before a respin: `pcb/out/verification_v3q.md` for what changed and why, with the checks taken before ordering, and `pcb/out/electrical_audit_2026-10-03.md` for the external review it answers.

## Next revision

Firmware works around all three on v3q; none can be fixed in firmware.

- **Shutter onto an RTC pad.** It is on IO44 (module pin 36), which cannot wake the S3 from deep sleep: only GPIO 0-21 can. So the camera can only light-sleep (~4 mA), and the deep sleep firmware does at a flat battery can only be woken by USB, through VBUS sense on IO16. **IO15 (module pin 8) is free** and an RTC pad; moving `SHUTTER` there keeps R26/C24 as they are and lets the button wake deep sleep, which is the largest power saving on this design. The pin table in `firmware/AGENTS.md` changes with it.
- **Replace U8, the panel's VCOM oscillator.** The 74LVC1G14 RC oscillator driving EXTCOMIN draws ~2 mA, measured by bridging C18: half of light sleep, and nearly all of deep sleep. The likely cause, not yet measured: the RC's slow edges hold the Schmitt input mid-rail, where a CMOS gate conducts straight through. Options, cheapest in current first: a nano-power timer (TPL5010-class, tens of nA) or a 32 kHz crystal oscillator with a divider, still on EXTCOMIN with EXTMODE high; or EXTCOMIN driven by an S3 RTC pin from the ULP, which keeps VCOM running through deep sleep for µA. Not EXTMODE low with software VCOM: deep sleep would then leave the panel on DC.
- **J5 battery polarity marking.** The `+` silkscreen fell off the board edge, and there is no reverse-polarity protection.
