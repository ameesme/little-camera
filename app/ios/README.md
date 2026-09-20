# Little Camera — iOS bridge

This app pulls photos off the camera over BLE and uploads them to the server, authenticating as the camera itself, and carries new firmware the other way. It never signs anyone in: registering, linking and approving subscribers all happen on the web. Wire format and the sync algorithm are `../../docs/protocol.md` §3.5, who-does-what is `../../docs/flows.md`.

SwiftUI + CoreBluetooth + CryptoKit, iOS 17, Swift 5.9, no packages.

```
brew install xcodegen          # once
cd app/ios && xcodegen generate && open LittleCamera.xcodeproj
```

Set your team under Signing & Capabilities, since the spec leaves `DEVELOPMENT_TEAM` empty, and run on a **physical iPhone** because the simulator has no Bluetooth.

On first run, replace the prefilled `https://lttl.cam` with your own domain if you are running your own; only the domain goes in, the app appends `/api/...` itself. Press the camera's shutter so it wakes and shows up as `lc-XXXX`, tap `USE THIS CAMERA`, then type the six digits off the camera's screen into iOS's passkey prompt. iOS keeps the bond, the secret goes to the Keychain, and neither is asked for again.

## Sending firmware to the camera

`FirmwareUpdater` is protocol §3.7. The card on the main screen shows what the camera runs against what the server has published, and the button only appears when the camera is new enough to say which version it has (Info version 2), has room in its spare slot, is connected, and is not syncing. The app downloads the image, checks its size and SHA-256 before it touches the camera, then streams ~500-byte chunks and rewinds whenever the camera names an offset.

Two kinds of flow control run at once and conflating them is the easy mistake: `canSendWriteWithoutResponse` is this phone's controller having room, while the camera's window is what its main loop can stage while it writes flash. Keep to both. Nothing the app gets wrong can brick the camera, since the image lands in the slot the camera is not running from and is checked there before anything is armed, so the worst case is a wasted minute and another go.

## Known unknowns

Nothing here has been compiled or run yet, since it was written without Xcode. The iOS 17 APIs (`@Observable` + `@MainActor` on `AppModel`, `AsyncStream.makeStream`, `beginBackgroundTask`'s closure isolation) are used the way the SDK documents them, but nobody has seen the compiler's actual complaints. Then:

- **Background wake latency.** The app keeps a connect pending forever, so iOS completes it when the camera advertises and relaunches the app to do it. How fast that is for a killed app is undocumented, and the camera's 30 s advertising window is the whole budget.
- **MTU.** The camera asks for 512 and iOS grants 185 or 251. Logged, but nothing depends on it.
- **Keychain accessibility.** The secret is `AfterFirstUnlock` so a background sync after a reboot can read it. Untested.
- **Update throughput.** A 1.5 MB image at iOS's write-without-response rate should be a minute or two, but the camera writes flash from its main loop and the real ceiling is probably there. A log full of rewinds means the camera's window is too big for its loop, not the other way round.
- **Whether iOS keeps a write-without-response stream alive in the background** for a whole image. The update runs inside the same ~30 s `beginBackgroundTask` window as a sync, which is enough for photos and probably not for firmware, so expect to keep the app open for this one.
- The app icon is a generated black square with a white dithered dot. Replace it with a hand-made one some day.
