# Deploying lttl.cam

**https://lttl.cam is live**, served by Vercel from `site/` on every push to `main`.

| | |
|---|---|
| Vercel project | `lttl-cam`, scope `mees-projects-2bb0cea8` (Hobby) |
| Production branch | `main` |
| Domain | `lttl.cam` itself, no subdomains |
| Payload | 204 KB of HTML plus 52 KB of JavaScript |

## There is no build

`site/index.html` is hand-written and has no dependencies, no env vars and no fetches: the firmware screens are inlined as base64 PNGs and the device is raymarched in WebGL by `camera-element.js`, the one file the page loads. `site/` is not a pnpm workspace member, which is why `vercel.json` empties both the install and the build command — otherwise Vercel installs and builds the whole monorepo.

Config lives in `vercel.json` and `.vercelignore` at the repo root, so a Git-triggered build and a CLI one agree. `vercel.json` sets `outputDirectory: "site"`, `cleanUrls`, a year of immutable caching on `/fonts/*` and `nosniff` + `strict-origin-when-cross-origin` on everything. `.vercelignore` keeps the firmware, hardware, app, server, packages, docs and every `.md` out of the upload.

## Things worth knowing

**The fonts are licensed.** PP Neue Montreal is a commercial Pangram Pangram face under Amaranth Studio's webfont licence, and `ameesme/little-camera` is public. Serving it from lttl.cam stays inside the licence, but having the binaries in public history counts as redistributing them. Mees made that call knowingly — see `fonts/README.md`.

**`www.lttl.cam` has no certificate.** The zone's wildcard `ALIAS` resolves it to the Vercel edge, but no project claims the hostname, so a visitor gets a certificate error. One command fixes it. It is left undone because it would take `www` out of the subdomains the micro-blog hands to owners:

```bash
vercel domains add www.lttl.cam lttl-cam --scope mees-projects-2bb0cea8
# then set it to redirect to lttl.cam in Settings → Domains
```

**Deployment Protection is on**, which is Vercel's default. It redirects the `*.vercel.app` URL to Vercel SSO and does not cover the production domain, which is why `lttl.cam` is public while the preview URL asks for a login. To share the preview URL, turn it off in Settings → Deployment Protection; the CLI cannot do it. `vercel curl <url>` reaches protected deployments in the meantime.

**`lttl.cam` can hold both.** The micro-blog only answers under `/api` and `/app` on that domain and every owner gets `<handle>.lttl.cam`, so `/` stays the landing page's either way. Only the bare domain is attached to Vercel and `*.lttl.cam` is deliberately left free. When the blog ships, point `lttl.cam` at the VPS and set `LANDING_DIR` to `site/`, which is what `server/deploy/` already defaults to, and this page becomes the deploy of record for nothing but the preview. One more thing: Vercel Hobby is for non-commercial use, so if lttl.cam grows a shop the scope needs Pro.

## Rollback

```bash
vercel rollback --scope mees-projects-2bb0cea8
```
