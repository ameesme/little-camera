# AGENTS.md (repository root)

Monorepo for Little Camera. Read the part-specific guide before touching that part:

| Part | Guide | Notes |
|---|---|---|
| `firmware/` | `firmware/AGENTS.md` | Hardware contract. Pins, panel constraints, sleep rules, storage format. Load-bearing; read it whole. |
| `server/` | `server/README.md` | Hono + `node:sqlite`. No ORM, no SPA. |
| `ios/` | `ios/README.md` | SwiftUI + CoreBluetooth, no dependencies. Generated with XcodeGen. |
| `packages/pbm/` | inline docs | Pure TypeScript, no Node-only APIs except `zlib` and `crypto`. |
| `docs/protocol.md` | | **The contract** between camera, phone and server. Change it first, then the three implementations. |

## Rules that cross parts

- **Black and white only.** No colour tokens anywhere: CSS, SwiftUI, emails, app icon, QR page. Grey is expressed as a 2 px checker (`.dither50` on the web, `Dither` view on iOS). If a design needs emphasis, invert.
- **PBM is the photo.** 320×240 P4 files as written by the firmware are stored and served unchanged; PNG is a presentation format generated on the fly and cached by hash. Never re-dither, scale or anti-alias a photo on the server.
- **The phone is a bridge, not a user.** It authenticates as the camera (`Authorization: Camera <id>:<secret>`). Anything requiring a human decision lives on the web or is a camera-scoped API call.
- **Existing photos on devices are precious.** The partition table (`default_8MB.csv`), the LittleFS mount parameters and the on-flash file format are frozen. See `firmware/AGENTS.md`.
- Commit style: lowercase `feat:`/`fix:`/`chore:` with an optional scope (`feat(server): …`) and a present-participle description, matching the existing history.
- Justify non-obvious decisions in a comment with the tradeoff. Prefer the simplest thing that works on real hardware and real phones.
