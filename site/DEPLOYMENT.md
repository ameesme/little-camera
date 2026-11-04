# Deploying lttl.cam

**https://lttl.cam is live on the Hetzner VPS**, served by Caddy from `/opt/little-camera/site`. The server and blogs run on the same host; see [the production deployment guide](../server/deploy/README.md).

| | |
|---|---|
| Production host | `135.181.83.145`, SSH alias `lttl-cam` |
| Checkout | `/opt/little-camera` |
| Production branch | `main` |
| Landing page | `lttl.cam/`, Caddy bind mount from `site/` |
| Vercel preview project | `lttl-cam`, scope `mees-projects-2bb0cea8` |

## Updating the landing page

Commit and push the changes, then pull them on the VPS:

```bash
git add <changed-files>
git commit -m 'fix: describing the change'
git push origin main
ssh lttl-cam 'cd /opt/little-camera && git pull --ff-only origin main'
curl -I https://lttl.cam/
```

The mounted static files update immediately; no image build or Caddy restart is needed. A push alone does not update production. If the change includes server code, follow the production guide to rebuild its container too.

## There is no build

`site/index.html` is hand-written and has no dependencies, no env vars and no fetches: the firmware screens are inlined as base64 PNGs and the device is raymarched in WebGL by `camera-element.js`, the one file the page loads. `site/` is not a pnpm workspace member, which is why `vercel.json` empties both the install and the build command — otherwise Vercel installs and builds the whole monorepo.

Vercel preview config lives in `vercel.json` and `.vercelignore` at the repo root, so a Git-triggered build and a CLI one agree. `vercel.json` sets `outputDirectory: "site"`, `cleanUrls`, a year of immutable caching on `/fonts/*` and `nosniff` + `strict-origin-when-cross-origin` on everything. `.vercelignore` keeps the firmware, hardware, app, server, packages, docs and every `.md` out of the upload. These settings do not configure Caddy in production.

## Things worth knowing

**The fonts are licensed.** PP Neue Montreal is a commercial Pangram Pangram face under Amaranth Studio's webfont licence, and `ameesme/little-camera` is public. Serving it from lttl.cam stays inside the licence, but having the binaries in public history counts as redistributing them. Mees made that call knowingly — see `fonts/README.md`.

**The domain is shared.** Caddy forwards `/api/*` and `/app/*` to the Node server and serves the landing page for other paths on the apex domain. Blog subdomains go to the Node server, with certificates issued on demand for approved names.

## Vercel preview rollback

This affects the Vercel deployment only; it does not roll back the VPS.

```bash
vercel rollback --scope mees-projects-2bb0cea8
```
