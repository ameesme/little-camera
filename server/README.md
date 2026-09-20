# Micro-blog server

One Node process serves three things, told apart by the `Host` header:

| Host | What |
|---|---|
| `BASE_DOMAIN` (the apex) | `/api/*` and `/app/*`, and nothing else |
| `<handle>.BASE_DOMAIN` | that owner's blog, at its own root |
| anything else | 404 |

The apex root is not ours. A landing page lives at `https://lttl.cam/`, served
by the proxy in front (`deploy/`), and only the two prefixes reach this
process:

| Path | What |
|---|---|
| `/api/camera/*` | the camera API (protocol §4) |
| `/api/firmware/*` | firmware releases (protocol §4.7), unauthenticated |
| `/app/*` | registration, sign-in links, `/app/me` (the verification picture, then the owner's page), the dev mailbox |

Both prefixes are `API_PREFIX` / `APP_PREFIX` in `src/config.ts`; links and
emailed URLs go through `appUrl()` / `apiUrl()` so they are written down once.
`GET /` redirects into `/app` so that running this server on its own still
lands somewhere useful.

Stack: Hono, Hono JSX for server-rendered pages, `node:sqlite` (built into Node 22, no native module), `nodemailer`, `qrcode`, `jsqr`. No client framework; the blog's one script only makes the subscribe button slide open and comments appear without a reload.

Read `docs/protocol.md` for the camera API and `docs/flows.md` for who does what.

## Run it

```
pnpm install
pnpm dev            # http://localhost:3000/app, blogs at http://<handle>.localhost:3000
pnpm test
pnpm seed           # a demo blog "mees" with the mockup's pictures and comments
```

`*.localhost` resolves to 127.0.0.1 in every modern browser, so `http://mees.localhost:3000` works with no hosts-file edits. Without `SMTP_URL` no mail leaves the machine: every message is stored and readable at `http://localhost:3000/app/dev/mailbox`.

### Walk the registration flow without a camera

```
# 1. register at http://localhost:3000/app/register, open the dev mailbox, click the link
# 2. /app/me shows the QR; note the code under it, e.g. LC:KQ7M2X
pnpm fake-camera --code LC:KQ7M2X     # "photographs" the code and uploads it as a camera
# 3. /app/me flips to LINKED; the blog is at http://<handle>.localhost:3000
pnpm fake-camera --fixture moon       # post a picture
pnpm fake-camera --status             # what the app would see
pnpm fake-camera --firmware 0.1.0 --status   # …as a camera with an update waiting
```

The fake camera has id `7cdfa1e2b3c4` (short code `MM48F3`, the typed fallback on `/me`).

## Environment

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `3000` | |
| `BASE_DOMAIN` | `localhost:3000` | apex host, with port if non-standard (`lttl.cam` in production) |
| `PUBLIC_SCHEME` | `http` | `https` behind Caddy |
| `DATA_DIR` | `./data` | SQLite file + `photos/<camera>/<sha256>.pbm` |
| `FIRMWARE_DIR` | `<DATA_DIR>/firmware` | `manifest.json` + the `.bin` images it names (protocol §4.7) |
| `SESSION_SECRET` | dev value | signs the owner and subscriber cookies; required in production |
| `SMTP_URL` | unset | `smtps://user:pass@host:465`; unset = dev mailbox only |
| `MAIL_FROM` | `Little Camera <camera@localhost>` | |
| `DISPLAY_TZ` | `Europe/Amsterdam` | zone for the dates under photos |
| `NODE_ENV` | `development` | `production` disables `/dev/mailbox` |

## Layout

```
migrations/*.sql          schema, applied in name order on start (unix seconds everywhere)
src/index.ts              boot: config, db, jobs, http
src/app.ts                host router
src/routes/camera.ts      /api/camera/* (protocol §4)
src/routes/firmware.ts    /api/firmware/* (protocol §4.7)
src/routes/web.tsx        /app/* pages
src/routes/blog.tsx       one blog
src/views/*.tsx           markup; layout.tsx carries the mockup's CSS
src/lib/firmware.ts       the release manifest and the digests, read off disk
src/lib/ingest.ts         what happens to an uploaded PBM (dedupe, dating, QR, avatar)
src/lib/qr.ts             decoding a QR out of a 1-bit camera shot
src/lib/simulate.ts       a pretend camera shot of the QR page (tests, fake-camera)
src/lib/tokens.ts         emailed links (48 h subscriber, 1 h owner login, 24 h email verify)
src/lib/email.ts          outbox + drainer
src/emails/index.ts       the messages, plain text + minimal HTML
src/jobs/newsletter.ts    every 10 min: one email per subscriber per batch of new pictures
src/jobs/purge.ts         hourly: expired tokens/codes, unbound photos older than 7 days
src/repo/*.ts             plain SQL, one file per table
test/                     vitest; in-memory database, fake clock, real fixture photos
```

## Production

See `deploy/` for Docker Compose + Caddy. The image is built from the repo root with `server/Dockerfile`.
