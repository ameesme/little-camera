# Little Camera

A small black-and-white camera that posts to its own tiny blog, and everything around it.

```
firmware/    ESP32-S3 firmware (PlatformIO). Viewfinder, gallery, flash storage, BLE sync and update.
hardware/    The board it runs on: KiCad schematic + PCB, written by Python.
app/ios/     Bridge app (SwiftUI + CoreBluetooth). Carries photos off the camera and firmware back to it.
server/      Micro-blog + API (TypeScript, Hono, SQLite). One subdomain per owner.
  deploy/    Docker Compose + Caddy (wildcard TLS) for a VPS.
site/        The lttl.cam teaser, static on Vercel.
packages/    Shared TypeScript: PBM parsing, PNG encoding, short codes.
docs/        protocol.md (the contract), flows.md (who does what), mood.md (the camera as a small creature).
```

The service is `lttl.cam`, and a blog is one label under it (`mees.lttl.cam`). The landing page owns `lttl.cam/`, and everything the server answers on that domain sits under `/api` for machines or `/app` for people, so a proxy can tell the two apart.

Everything is black and white. Where a grey is unavoidable it is a 2 px checker.

## Step 0: back up the photos on your camera

Do this before you flash anything from here. It reads the flash and writes nothing:

```
cd firmware/tools/export
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python dump_flash.py --port /dev/cu.usbmodem*      # backup/<timestamp>/NNNN.pbm + .png
```

The partition table and file format are unchanged, so flashing keeps the photos on the device too, and the first sync uploads them all.

## Firmware

```
cd firmware
pio run -e v3q -t upload -t monitor
cd tools/preview && make scenes     # the UI without hardware, BMPs in out/
```

Read `firmware/AGENTS.md` before anything hardware-facing.

Press = shoot. Hold 0.7 s in the viewfinder = gallery (press = next, hold = back). On the review screen press = save, hold 1.5 s = trash. Asleep: hold 1 s to wake, a tap only chirps.

The camera has a mood (`docs/mood.md`): it sleeps with a smile after a photo, loses it over a week, and chirps for attention. The sounds are composed, not recorded, and `cd firmware/tools/chirp && make bands` renders them to WAV.

New firmware can also come over Bluetooth from the app (`docs/protocol.md` §3.7). It is written to the slot the camera is not running from, and the bootloader only points at it once every byte has arrived and the SHA-256 matches, so a failed update costs time and nothing else. `ota revert` on the USB console is the way back after you flash over the wire.

## Hardware

v3q, 62.8 × 42.82 mm, four layers, 80 parts, KiCad 10. Schematic `hardware/pcb/little_camera.kicad_sch`, board `hardware/pcb/little_camera.kicad_pcb`, and `little_camera_2d_view.svg` next to them if you have no KiCad. Neither is drawn by hand: `hardware/pcb/gen/design.py` holds every part and connection and the rest of `gen/` builds, places and routes from it. See `hardware/README.md`.

Pins are shared with the firmware and written down in `firmware/AGENTS.md`. Change them in both places.

## Server

```
pnpm install
pnpm dev      # http://localhost:3000/app, blogs at http://<handle>.localhost:3000
pnpm test
pnpm seed     # demo blog at http://mees.localhost:3000
```

Without SMTP settings every email lands in `/app/dev/mailbox`. `server/README.md` has the environment variables and the fake camera; `server/deploy/` has production, including how a firmware release is published.

## iOS app

```
cd app/ios && xcodegen generate && open LittleCamera.xcodeproj
```

Needs a real device, because Bluetooth. Set the server URL on first launch. The app never signs in: it identifies as the camera it is paired with.

## The whole flow

Register on the web, then photograph the QR code on `/app/me` with the camera and press send: the app uploads it, the server binds the camera to your blog, and every photo you send from then on lands on `https://<handle>.lttl.cam`. Friends join with their email and get one link per batch of new pictures. Step by step in `docs/flows.md`.
