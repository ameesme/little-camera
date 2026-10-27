# Little Camera — iOS bridge

This app pulls photos off the camera over BLE and uploads them to the server, authenticating as the camera itself. It never signs anyone in: registering, linking and approving subscribers all happen on the web. Wire format and the sync algorithm are `../../docs/protocol.md` §3.5, who-does-what is `../../docs/flows.md`.

SwiftUI + CoreBluetooth + CryptoKit, iOS 17, Swift 5.9, no packages.

```
brew install xcodegen          # once
cd app/ios && xcodegen generate && open LittleCamera.xcodeproj
```

Set your team under Signing & Capabilities, since the spec leaves `DEVELOPMENT_TEAM` empty, and run on a **physical iPhone** because the simulator has no Bluetooth.

On first run, replace the prefilled server URL with your own domain. Press the camera's shutter so it wakes and shows up as `lc-XXXX`, tap `USE THIS CAMERA`, then type the six digits off the camera's screen into iOS's passkey prompt. iOS keeps the bond, the secret goes to the Keychain, and neither is asked for again.

## Known unknowns

Nothing here has been compiled or run yet, since it was written without Xcode. The iOS 17 APIs (`@Observable` + `@MainActor` on `AppModel`, `AsyncStream.makeStream`, `beginBackgroundTask`'s closure isolation) are used the way the SDK documents them, but nobody has seen the compiler's actual complaints. Then:

- **Background wake latency.** The app keeps a connect pending forever, so iOS completes it when the camera advertises and relaunches the app to do it. How fast that is for a killed app is undocumented, and the camera's 30 s advertising window is the whole budget.
- **MTU.** The camera asks for 512 and iOS grants 185 or 251. Logged, but nothing depends on it.
- **Keychain accessibility.** The secret is `AfterFirstUnlock` so a background sync after a reboot can read it. Untested.
- The app icon is a generated black square with a white dithered dot. Replace it with a hand-made one some day.
