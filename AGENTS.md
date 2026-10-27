# AGENTS.md (repository root)

Monorepo for Little Camera. Read the guide for a part before you touch it:

| Part | Guide | Notes |
|---|---|---|
| `firmware/` | `firmware/AGENTS.md` | Pins, panel limits, sleep rules, storage format. Load-bearing, read it whole. |
| `server/` | `server/README.md` | Hono + `node:sqlite`. No ORM, no SPA. The domain is cameras, blogs, photos, subscribers, firmware — one file each in `core/`, which routes call rather than the database. |
| `app/ios/` | `app/ios/README.md` | SwiftUI + CoreBluetooth, no dependencies, XcodeGen. Staged UI: one screen does one job. |
| `hardware/pcb/` | `hardware/README.md` | KiCad 10, written by the Python in `pcb/gen/` rather than by hand. Pin changes land in `firmware/AGENTS.md` too. |
| `site/` | `site/README.md` | Static teaser on Vercel. `vercel.json` and `.vercelignore` stay at the root. |
| `packages/pbm/` | inline docs | Pure TypeScript, no Node-only APIs except `zlib` and `crypto`. |
| `docs/protocol.md` | | **The contract** between camera, phone and server. Change it first, then the three implementations. |

## Rules that cross parts

- **Black and white only.** No colour tokens anywhere: CSS, SwiftUI, emails, app icon, QR page. Grey is a 2 px checker (`.dither50`, the `Dither` view). For emphasis, invert.
- **PBM is the photo.** The firmware's 320×240 P4 files are served unchanged; PNG is generated on the fly and cached by hash. Never re-dither, scale or anti-alias a photo on the server.
- **The phone is a bridge, not a user.** It authenticates as the camera (`Authorization: Camera <id>:<secret>`). Human decisions live on the web or in camera-scoped API calls.
- **Generated hardware output is not committed.** Only the KiCad sources, their libraries and `pcb/fab/` are tracked; `pcb/out/` and the heavy parts of `pcb/gen/` are rebuilt from the scripts.
- **Photos already on a device are precious.** `default_8MB.csv`, the LittleFS mount parameters and the on-flash file format are frozen.
- Commits: lowercase `feat:`/`fix:`/`chore:`, optional scope, present-participle description.
- Justify a non-obvious decision in a comment, with the tradeoff. Prefer the simplest thing that works on real hardware and real phones.
