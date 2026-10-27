# Micro-blog server

One Node process serving two things, told apart by the `Host` header. On `BASE_DOMAIN` it answers under `/api/*` and `/app/*` and nowhere else, because `/` there belongs to the landing page in front of it; `<handle>.BASE_DOMAIN` is that owner's blog, at its own root; anything else gets a 404.

| Path | What |
|---|---|
| `/api/camera/*` | the camera API (`docs/protocol.md` §4) |
| `/api/firmware/*` | firmware releases (§4.7), no auth |
| `/app/*` | registration, sign-in links, `/app/me`, the dev mailbox |

Both prefixes are `API_PREFIX` / `APP_PREFIX` in `src/config.ts` and every link goes through `appUrl()` / `apiUrl()`, so they are written down once. `GET /` redirects into `/app` so a server running on its own still lands somewhere.

Hono, Hono JSX, `node:sqlite` (in Node 22, so no native module), `nodemailer`, `qrcode`, `jsqr`. No client framework. `docs/protocol.md` has the camera API, `docs/flows.md` has who does what.

```
core/     the five things in this domain, one file each, and the only place each is written
          cameras      saying hello, and binding to a blog by photographing its QR code
          blogs        registering one, signing its owner in, and who is reading it
          photos       an upload: parse, dedupe, date, store, maybe bind, maybe avatar; and comments
          subscribers  adding, approving, blocking, joining, redeeming a link, and the mail for each
          firmware     the releases on disk, and whether one is newer than what a camera runs
routes/   HTTP only: parse the request, call core/, render
views/    the HTML, as whole pages
repo/     plain SQL, one file per table
lib/      plumbing with no opinions: dates, ids, tokens, cookies, mail, QR
```

A blog is a handle, a name, a picture and the one email address that owns it, so the owner is a column and not a table of their own. A camera points at the blog it posts to, which lets a camera move to another blog without taking its old photos along.

A route never reaches past `core/` into `repo/` for something `core/` already does. That is the rule that keeps "approve a subscriber and welcome them" in one place instead of four. Plain reads go straight to `repo/`.

```
pnpm install
pnpm dev      # http://localhost:3000/app, blogs at http://<handle>.localhost:3000
pnpm test
pnpm seed     # a demo blog "mees" with the mockup's pictures and comments
```

`*.localhost` resolves to 127.0.0.1 in every modern browser, so no hosts-file edit. Without `SMTP_URL` no mail leaves the machine: every message is readable at `/app/dev/mailbox`, which `NODE_ENV=production` disables.

## Walk the registration flow without a camera

```
# register, open /app/dev/mailbox, click the link; /app/me then shows a QR with a code under it
pnpm fake-camera --code LC:KQ7M2X     # "photographs" the code and uploads it as a camera
pnpm fake-camera --fixture moon       # post a picture
pnpm fake-camera --status             # what the app would see
pnpm fake-camera --firmware 0.1.0 --status   # the same, as a camera with an update waiting
```

The fake camera is id `7cdfa1e2b3c4`, short code `MM48F3`.

## Environment

Defaults in `src/config.ts` are the dev ones and boot without a `.env`. The three that matter in production:

| Variable | Why |
|---|---|
| `BASE_DOMAIN` | the domain on which blogs are registered, with the port if it is non-standard |
| `SESSION_SECRET` | signs the owner and subscriber cookies, required |
| `SMTP_URL` | `smtps://user:pass@host:465`; unset means the dev mailbox only |

`DATA_DIR` holds the SQLite file and `photos/<camera>/<sha256>.pbm`, and is the only thing to back up. `FIRMWARE_DIR` defaults to `<DATA_DIR>/firmware` and holds `manifest.json` plus the `.bin` images it names. `PUBLIC_SCHEME`, `MAIL_FROM` and `DISPLAY_TZ` are the rest.

## Production

`deploy/` has Docker Compose + Caddy. The image is built from the repo root with `server/Dockerfile`.
