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
