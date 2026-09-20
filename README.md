# Little Camera

A small black-and-white camera that posts to its own tiny blog, and everything around it.

```
firmware/    ESP32-S3 firmware (PlatformIO). Viewfinder, gallery, flash storage, BLE sync and update.
ios/         Bridge app (SwiftUI + CoreBluetooth). Carries photos off the camera and firmware back to it.
server/      Micro-blog + API (TypeScript, Hono, SQLite). One subdomain per camera owner.
packages/    Shared TypeScript: PBM parsing, PNG encoding, short codes.
deploy/      Docker Compose + Caddy (wildcard TLS) for a VPS.
docs/        protocol.md (BLE + HTTP contract), flows.md (who does what), mood.md (the camera as a small creature).
```

The service is `lttl.cam`. A blog is one label under it (`mees.lttl.cam`); the
landing page owns the apex root, and everything the server answers there sits
under `/api` (machines) or `/app` (people) so a proxy can tell the two apart.

Everything is black and white. Where a grey is unavoidable it is a 2 px checker.

## Step 0: back up the photos already on your camera

Before flashing anything from this repo, pull the photos that are on the device today. This reads the flash and writes nothing:

```
cd firmware/tools/export
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python dump_flash.py --port /dev/cu.usbmodem*      # writes backup/<timestamp>/NNNN.pbm + .png
```

Then flash. New firmware keeps the same partition table and file format, so the photos stay on the device too; the first sync uploads all of them to your blog.

## Firmware

```
cd firmware
pio run -e xiao_shutter -t upload -t monitor
```

Read `firmware/AGENTS.md` before changing anything hardware-facing. The UI can be previewed without hardware:

```
cd firmware/tools/preview && make scenes   # BMPs in out/
```

Gestures: press = shoot. Hold 0.7 s in the viewfinder = gallery (press = next, hold = back). On the review screen press = save, hold 1.5 s = trash. Asleep: hold 1 s to wake; a tap only makes it chirp.

Firmware can also arrive over Bluetooth from the app (`docs/protocol.md` §3.7).
It lands in the app slot the camera is *not* running from, and the bootloader
is pointed at it only once every byte is there and its SHA-256 checks out — so
a failed update costs the time and nothing else. After one, `ota revert` on the
USB console is the way back to a slot you flashed over the wire.

The camera has a mood (`docs/mood.md`): it sleeps with a smile after a photo, loses it over a week, and chirps for attention. The sounds are composed, not recorded; listen to them without hardware:

```
cd firmware/tools/chirp && make bands       # out/band-{sad,glum,content,happy}.wav + out/ladder.wav
```

## Server

```
pnpm install
pnpm dev                      # http://localhost:3000/app, blogs at http://<handle>.localhost:3000
pnpm test
pnpm seed                     # demo blog with the mockup's pictures at http://mees.localhost:3000
pnpm --filter @little-camera/server fake-camera --code LC:XXXXXX   # play the bridge app (see server/README.md)
```

Without SMTP settings every email lands in `http://localhost:3000/app/dev/mailbox`. See `server/README.md` for the environment variables and `deploy/` for production, including how a firmware release is published.

## iOS app

```
cd ios && xcodegen generate && open LittleCamera.xcodeproj
```

Needs a real device (Bluetooth). Set the server URL on first launch. The app never signs in: it identifies as the camera it is paired with. Registration and verification happen on the web.

## Flow in one paragraph

Register on the web with your email and a handle. Open `/app/me` on your phone: it shows a QR code. Photograph it with the camera and press send. The app in your pocket uploads the photo; the server reads the QR and binds the camera to your blog. From then on every photo you send ends up on `https://<handle>.lttl.cam`. Friends join with their email; you approve them in the app; they get one email per batch of new pictures with a 48-hour link that lets them see everything and comment.
