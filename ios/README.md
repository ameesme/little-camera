# Little Camera — iOS bridge

The phone is a pipe. It pulls photos off the camera over Bluetooth LE and uploads them to the server *as the camera* (`Authorization: Camera <id>:<secret>`). It never signs in as a person; registering, linking and approving subscribers happen on the web or through camera-scoped API calls. Wire format: `../docs/protocol.md`. Who-does-what: `../docs/flows.md`.

SwiftUI + CoreBluetooth + CryptoKit, iOS 17, Swift 5.9, no packages. Black and white only.

## Build

```
brew install xcodegen          # once
cd ios
xcodegen generate
open LittleCamera.xcodeproj
```

Set your team under Signing & Capabilities (the spec leaves `DEVELOPMENT_TEAM` empty) and run on a **physical iPhone**: the simulator has no Bluetooth. Bundle id `me.amees.littlecamera`; change it in `project.yml` if you sign with another team.

## First run

1. **Server URL.** Prefilled with `https://littlecamera.example`; type the real apex (`https://your-domain`). It is stored in UserDefaults.
2. **Find the camera.** Press the camera's shutter so it wakes and advertises; it appears as `lc-XXXX` (the same name is on its screen). Tap `USE THIS CAMERA`. The CoreBluetooth identifier is stored; from now on the app always keeps a connect pending to that peripheral.
3. **Pairing.** On the first connection the app reads the encrypted `Secret` characteristic. iOS shows its own passkey prompt; type the six digits the camera displays (`pair 123456`). The bond is kept by iOS, the 16-byte secret goes to the Keychain, and neither is asked for again.
4. **Hello.** The app calls `POST /api/camera/hello` (trust on first use) and starts syncing.

To link the camera to a blog, open `https://<apex>/me` on the phone and photograph the QR code with the camera; the app shows the six-character short code as the fallback to type there.

## What a sync does

On every connection (and on `SYNC NOW`), `SyncEngine` runs protocol §3.5:

```
read Info → read Secret if not in Keychain → SET_TIME(now)
LIST(from 1, unsynced only) → verify seq + CRC-32
for each entry: GET(index, offset) → verify → upload (200/201/409 = success) → ACK
read Info again; repeat while unsynced_count > 0 (max 3 passes)
```

- 200, 201 and 409 from `POST /photos` all mean the server has the file, so the camera is ACKed in every case. SHA-256s of confirmed files are kept in UserDefaults so a lost ACK does not cause a second upload.
- A GET cut off by a disconnect keeps the bytes it got (in memory) and resumes from that offset on the next connection.
- `X-Captured-At` / `X-Captured-At-Source` are only sent when the file has no `t=` and the dating rule (§2) yields an estimate.
- Progress and the last 20 log lines are on screen.

## Background behaviour

`UIBackgroundModes: bluetooth-central`, a `CBCentralManager` with a restore identifier, and a **connect that is always pending** to the paired peripheral. iOS does not time pending connects out; when the camera wakes and advertises, iOS completes the connect — relaunching the app in the background if it was suspended or killed — and `willRestoreState` hands the peripheral back. The sync then runs inside a `beginBackgroundTask` window (~30 s), which is enough for a handful of 9.6 KB photos.

The camera only advertises while awake (10 s idle, 30 s when it has unsynced photos, and it stays awake while connected). Practical rule: take the picture, press send, and the phone in the pocket does the rest within half a minute.

## Known unknowns

Nothing in this directory has been compiled or run yet; it was written without Xcode. Look first at:

- **MTU.** The camera asks for 512; iOS grants 185 or 251 depending on device and iOS version. The negotiated value is logged (`maximumWriteValueLength(for: .withoutResponse) + 3`) but no code depends on it.
- **Background wake latency.** How quickly iOS completes a pending connect for a backgrounded or killed app is not documented and varies with the phone's state. The camera's 30 s advertising window is the budget.
- **Pairing UI timing.** The passkey prompt is driven by iOS when the encrypted read happens; if the user dismisses it the read fails with an ATT authentication error and the app shows it as an error line and retries on the next connection.
- **Keychain accessibility.** The secret is stored `AfterFirstUnlock` so a background sync after a reboot can read it. Not tested.
- **`@Observable` + `@MainActor` on `AppModel`, `AsyncStream.makeStream`, and `beginBackgroundTask`'s closure isolation** are used the way the iOS 17 SDK documents them, but the exact compiler diagnostics have not been seen.
- The app icon is a generated 1024×1024 PNG (black square, white dithered dot). Replace it with a hand-made one when there is time.
