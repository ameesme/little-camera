# Deploying the micro-blog

Production is live on the Hetzner VPS at `135.181.83.145` (SSH alias `lttl-cam`), checked out on `main` at `/opt/little-camera`. Docker Compose runs two containers: the Node server, with SQLite, photos and firmware images on a volume, and Caddy in front obtaining certificates for each approved blog subdomain on demand.

The domain is shared. Caddy forwards `/api/*` and `/app/*` to the server and serves everything else, `/` above all, from `LANDING_DIR`, so the landing page and the app deploy apart from each other. Blog subdomains go straight through, each owning the root of its own subdomain.

```
https://lttl.cam/            the landing page (static files from LANDING_DIR)
https://lttl.cam/app/…       the two pages an emailed link opens
https://lttl.cam/api/…       the camera API and firmware releases
https://mees.lttl.cam/       a blog
```

`lttl.cam` and the blog subdomains point at the VPS. Caddy serves the landing page from `/opt/little-camera/site`, mounted at `/srv/landing` (`LANDING_DIR` defaults to `../../site`). Vercel remains a separate preview deployment — see `site/DEPLOYMENT.md`.

## Updating production

**Pushing to `main` deploys.** A systemd timer on the VPS runs `autodeploy.sh` every minute: it fetches `main`, fast-forwards the checkout and rebuilds only what the new commits touch — the server for `server/`, `packages/` or the lockfiles, Caddy for its Caddyfile, both for `docker-compose.yml`, nothing for `site/` (a live mount). It pulls rather than being pushed to, so the box needs no inbound SSH and GitHub holds no key to it. The cost is up to a minute of lag and no build log on GitHub; the log is here instead:

```bash
curl -fsS https://lttl.cam/api/deploy-status      # result, commit, time, last 40 lines of output
ssh lttl-cam 'journalctl -u little-camera-deploy -n 50'
```

The status is public, so the script puts only its own and the build's output in it. A commit that fails is tried once, not every minute; push a fix, or rerun by hand with `ssh lttl-cam 'sudo FORCE=1 /opt/little-camera/server/deploy/autodeploy.sh'`. The next good deploy rebuilds from the last *good* one, so a site-only fix after a broken server commit still rebuilds the server. It refuses a checkout with local edits and a `main` that is not a fast-forward of it, rather than resolving either.

Installing it, once, after pulling a `main` that contains it:

```bash
ssh lttl-cam
sudo cp /opt/little-camera/server/deploy/little-camera-deploy.{service,timer} /etc/systemd/system/
sudo systemctl daemon-reload
sudo FORCE=1 /opt/little-camera/server/deploy/autodeploy.sh
sudo systemctl enable --now little-camera-deploy.timer
```

`FORCE=1` the first time because that pull already brought in the Caddy route and mount for the status page, so the script would otherwise see nothing new. The unit runs as root; if the checkout belongs to another account (git refuses a repository owned by someone else) or fetches with that account's key, `sudo systemctl edit little-camera-deploy` and set `User=` to it, in the `docker` group. `DEPLOY_BRANCH` in the unit picks a branch other than `main`.

By hand, as before, if the timer is off:

```bash
ssh lttl-cam 'cd /opt/little-camera && git pull --ff-only origin main && cd server/deploy && docker compose up -d --build server'
```

Do not copy edited files directly into the production checkout. The server runs from its Docker image, so pulling alone does not update it. Rebuilding recreates the server container and briefly interrupts requests; Caddy stays running and the data volume is retained. A pull also brings in any other commits on `main`, so check the production revision and working tree before deploying.

```bash
ssh lttl-cam 'cd /opt/little-camera && git status --short && git rev-parse --short HEAD && cd server/deploy && docker compose ps && docker compose logs --tail=30 server'
curl -fsS https://mees.lttl.cam/
curl -I https://lttl.cam/
```

Check the public response for the changed markup or CSS as well as a successful status. For landing-page-only updates, pulling updates the mounted files immediately without a server rebuild. When deploying by hand, a Caddyfile change needs `docker compose up -d --no-deps --force-recreate caddy`, not a reload: git replaces the file, and Caddy's single-file mount keeps seeing the old one.

## Setting up a new host

1. Point DNS at the box: `A  lttl.cam  → IP` and `A  *.lttl.cam → IP`.
2. `cp .env.example .env` and fill it in.
3. `docker compose up -d --build`.
4. Make the first blog from the app, or with `pnpm fake-camera`.

Backups: the `data` volume holds `little-camera.sqlite` (SQLite, WAL mode), `photos/` and `firmware/`. Either run `docker compose exec server sqlite3 /data/little-camera.sqlite ".backup /data/backup.db"` or copy the volume while the container is stopped.

## Publishing a firmware release

A release is files on the data volume with a manifest beside them (`docs/protocol.md` §4.7). Nothing about it is in the database, so publishing is a copy and an edit, and unpublishing is deleting a line.

**Bumping `firmware/src/version.h` on `main` publishes it.** Every deploy compares that version with the manifest; one it lacks is built on the VPS (`pio run -e v3q` in a throwaway `python:3.12-slim` container, toolchain cached in the `little-camera-pio` volume, so the first build takes a few minutes) and added to the top of the manifest on the `stable` channel, with the bump commit's subject as its notes. A failed build shows in `/api/deploy-status` and is retried with the next commit. Unpublishing stays manual.

By hand, for a release that should not wait for a push:

```
pio run -e v3q                              # in firmware/, on your machine
docker compose cp ../../firmware/.pio/build/v3q/firmware.bin \
    server:/data/firmware/little-camera-0.2.0.bin
docker compose exec server sh -c 'cat >> /data/firmware/manifest.json'   # or edit it
```

```json
{
  "releases": [
    {
      "version": "0.2.0",
      "channel": "stable",
      "hardware": "v3q",
      "file": "little-camera-0.2.0.bin",
      "notes": "Firmware update over BLE.",
      "released_at": 1757789000
    }
  ]
}
```

The size and the SHA-256 are read off the file, never from the manifest: the digest is what the camera checks the image against after the phone has carried it, so it must not be something anybody types. `curl https://lttl.cam/api/firmware/latest` to check.

Local development needs none of this, see `server/README.md`.
