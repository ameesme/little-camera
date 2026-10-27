# Micro-blog server

One Node process serving three things, told apart by the `Host` header. `BASE_DOMAIN` itself serves the landing page, registration, `/me` and `/api/camera`; `<handle>.BASE_DOMAIN` is that owner's blog; anything else gets a 404.

Hono, Hono JSX, `node:sqlite` (in Node 22, so no native module), `nodemailer`, `qrcode`, `jsqr`. No client framework. `docs/protocol.md` has the camera API, `docs/flows.md` has who does what.

```
pnpm install
pnpm dev      # http://localhost:3000, blogs at http://<handle>.localhost:3000
pnpm test
pnpm seed     # a demo blog "mees" with the mockup's pictures and comments
```

`*.localhost` resolves to 127.0.0.1 in every modern browser, so no hosts-file edit. Without `SMTP_URL` no mail leaves the machine: every message is readable at `/dev/mailbox`, which `NODE_ENV=production` disables.

## Walk the registration flow without a camera

```
# register, open /dev/mailbox, click the link; /me then shows a QR with a code under it
pnpm fake-camera --code LC:KQ7M2X     # "photographs" the code and uploads it as a camera
pnpm fake-camera --fixture moon       # post a picture
pnpm fake-camera --status             # what the app would see
```

The fake camera is id `7cdfa1e2b3c4`, short code `MM48F3`.

## Environment

Defaults in `src/config.ts` are the dev ones and boot without a `.env`. The three that matter in production:

| Variable | Why |
|---|---|
| `BASE_DOMAIN` | the domain on which blogs are registered, with the port if it is non-standard |
| `SESSION_SECRET` | signs the owner and subscriber cookies, required |
| `SMTP_URL` | `smtps://user:pass@host:465`; unset means the dev mailbox only |

`DATA_DIR` holds the SQLite file and `photos/<camera>/<sha256>.pbm`, and is the only thing to back up. `PUBLIC_SCHEME`, `MAIL_FROM` and `DISPLAY_TZ` are the rest.

## Production

`deploy/` has Docker Compose + Caddy. The image is built from the repo root with `server/Dockerfile`.
