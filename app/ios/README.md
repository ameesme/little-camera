# Little Camera — iOS bridge

This app pulls photos off the camera over BLE and uploads them to the server, authenticating as the camera itself, and carries new firmware the other way. It also owns the blog now: making one, getting back into one, reading its feed and its subscribers. The one thing it cannot do is prove a blog belongs to the person holding the camera — only that blog's inbox can, which is what the `claim` stage is for (`../../docs/protocol.md` §4.9). Wire format and the sync algorithm are §3.5 and §3.7, who-does-what is `../../docs/flows.md`.

SwiftUI + CoreBluetooth + CryptoKit, iOS 26, Swift 6, no packages.

```
brew install xcodegen          # once
cd app/ios && xcodegen generate && open LittleCamera.xcodeproj
```

Set your team under Signing & Capabilities, since the spec leaves `DEVELOPMENT_TEAM` empty, and run on a **physical iPhone** for anything involving the camera, because the simulator has no Bluetooth. The server is `https://lttl.cam` and is not configurable yet.

Press the camera's button so it wakes, tap `CONNECT`, then type the six digits off the camera's screen into iOS's passkey prompt. iOS keeps the bond, the secret goes to the Keychain, and neither is asked for again.

Every stage renders in the simulator without a camera, which is how the screenshots are taken:

```
xcrun simctl launch booted me.amees.littlecamera -demo onboarding
```

`-demo <name>` seeds `Demo.swift` with a plausible camera, blog and feed and drops the app straight into that screen, with the radio and the server both switched off so nothing overwrites it. The names are the stages (`intro`, `setup`, `firmware`, `claim`, `onboarding`, `ready`) plus the screens inside them: `found`, `create`, `handle`, `email`, `email-taken`, `sent`, `bind`, `avatar`, `asked`. The `bind` screen turns the phone on its side, so a screenshot of it needs rotating.

## Layout

```
Model/    AppModel        which stage the app is in, and the three things below
          CameraSession   the link, what Info said, and the sync after every connection
          Blog            what the server says: linked or not, subscribers, profile picture
          FirmwareUpdate  installed against published, and the push between them
BLE/      CameraLink      one camera, one connection, all of it on a private queue
          Protocol        the byte layouts from docs/protocol.md §3
Sync/     SyncEngine      §3.5: SET_TIME, LIST, GET each with resume, upload, ACK
          FirmwareUpdater §3.7: chunks inside the camera's window, with rewind
          ServerClient    the HTTP camera API
UI/       Stages/         setup → firmware → claim or onboarding → ready
          Screens/        the two tabs, BlogTab and CameraTab
          Device/         the camera itself, a WKWebView around site/camera-element.js
          Ground          the landing page's dithered vignette, as a cached CGImage
```

The app is **staged**, not one long page. `AppModel.stage` decides which of six takes the screen: `intro` until the owner presses Get started, `setup` until a camera is picked, `firmware` while there is an update worth offering, then either `claim` (the camera already posts to a blog, so the owner's inbox has to let this phone in) or `onboarding` (no blog yet: make one or sign into one, photograph the code, take the picture that heads it), and finally `ready`, which is the two tabs. Onboarding asks one question per screen and never puts two fields on one.

The camera on screen is the same object as the one on lttl.cam: its panel shows the camera's own mood while it sleeps, and the icons it shows otherwise are lifted from `firmware/src/icons.h` by `site/tools/gen_icons.py`, so the app never invents artwork the device could not draw itself. A new panel icon starts as an SVG in `firmware/artifacts/fonts/`, goes into `icons.h` through the converter there, and reaches the app and the site through that script.

 `UI/Device/DeviceView.swift` is a `WKWebView` loading `Resources/device.html`, which loads `site/camera-element.js` straight out of the site directory — one file, no copy, listed in `project.yml`. Swift talks to it through `window.lc`, and the face it shows is the camera's own mood off Info (`docs/mood.md`), never something the app decided.

Every call to the server goes through one `perform` in `Blog`, so there is exactly one place where a failure becomes a line on screen.

## Sending firmware to the camera

`FirmwareUpdater` is protocol §3.7. The button only appears when the camera is new enough to say which version it has (Info version 2), has room in its spare slot, is connected, and is not syncing. The app downloads the image, checks its size and SHA-256 before it touches the camera, then streams ~500-byte chunks and rewinds whenever the camera names an offset.

Two kinds of flow control run at once and conflating them is the easy mistake: `canSendWriteWithoutResponse` is this phone's controller having room, while the camera's window is what its main loop can stage while it writes flash. Keep to both. Nothing the app gets wrong can brick the camera, since the image lands in the slot the camera is not running from and is checked there before anything is armed, so the worst case is a wasted minute and another go.

## Known unknowns

It builds and every stage has been looked at in the simulator. **Nothing below has been seen happen on a real camera**, because the simulator has no Bluetooth:

- **Background wake latency.** The app keeps a connect pending forever, so iOS completes it when the camera advertises and relaunches the app to do it. How fast that is for a killed app is undocumented, and the camera's 30 s advertising window is the whole budget.
- **MTU.** The camera asks for 512 and iOS grants 185 or 251. Logged, but nothing depends on it.
- **Everything over BLE.** Pairing, Info v3, sync, the avatar and the firmware push have only ever run against the simulator's absence of a radio.
- **Keychain accessibility.** The secret is `AfterFirstUnlock` so a background sync after a reboot can read it. Untested.
- **Update throughput.** A 1.5 MB image at iOS's write-without-response rate should be a minute or two, but the camera writes flash from its main loop and the real ceiling is probably there. A log full of rewinds means the camera's window is too big for its loop, not the other way round.
- **Whether iOS keeps a write-without-response stream alive in the background** for a whole image. The update runs inside the same ~30 s `beginBackgroundTask` window as a sync, which is enough for photos and probably not for firmware, so expect to keep the app open for this one.
- The app icon is a generated black square with a white dithered dot. Replace it with a hand-made one some day.
