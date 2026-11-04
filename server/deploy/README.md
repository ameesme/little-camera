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

Commit and push locally, then pull on the VPS. Do not copy edited files directly into the production checkout.

```bash
git add <changed-files>
git commit -m 'fix: describing the change'
git push origin main
ssh lttl-cam 'cd /opt/little-camera && git pull --ff-only origin main && cd server/deploy && docker compose up -d --build server'
```

The server runs from its Docker image, so pulling alone does not update it. Rebuilding recreates the server container and briefly interrupts requests; Caddy stays running and the data volume is retained. A pull also brings in any other commits on `main`, so check the production revision and working tree before deploying.

```bash
ssh lttl-cam 'cd /opt/little-camera && git status --short && git rev-parse --short HEAD && cd server/deploy && docker compose ps && docker compose logs --tail=30 server'
curl -fsS https://mees.lttl.cam/
curl -I https://lttl.cam/
```

Check the public response for the changed markup or CSS as well as a successful status. For landing-page-only updates, pulling updates the mounted files immediately without a server rebuild. For Caddy or Compose changes, apply the affected service configuration separately.

## Setting up a new host

1. Point DNS at the box: `A  lttl.cam  → IP` and `A  *.lttl.cam → IP`.
2. `cp .env.example .env` and fill it in.
3. `docker compose up -d --build`.
4. Make the first blog from the app, or with `pnpm fake-camera`.

Backups: the `data` volume holds `little-camera.sqlite` (SQLite, WAL mode), `photos/` and `firmware/`. Either run `docker compose exec server sqlite3 /data/little-camera.sqlite ".backup /data/backup.db"` or copy the volume while the container is stopped.

## Publishing a firmware release

A release is files on the data volume with a manifest beside them (`docs/protocol.md` §4.7). Nothing about it is in the database, so publishing is a copy and an edit, and unpublishing is deleting a line.

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
