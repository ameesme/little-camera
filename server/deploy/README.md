# Deploying the micro-blog

One VPS, Docker Compose, two containers: the Node server, with SQLite, the photos and the firmware images on a volume, and Caddy in front holding a wildcard certificate.

The domain is shared. Caddy forwards `/api/*` and `/app/*` to the server and serves everything else, `/` above all, from `LANDING_DIR`, so the landing page and the app deploy apart from each other. Blog subdomains go straight through, each owning the root of its own subdomain.

```
https://lttl.cam/            the landing page (static files from LANDING_DIR)
https://lttl.cam/app/…       the two pages an emailed link opens
https://lttl.cam/api/…       the camera API and firmware releases
https://mees.lttl.cam/       a blog
```

`LANDING_DIR` defaults to `../../site`, which is the teaser in this repo. Today that same `site/` is on Vercel and `lttl.cam` points there, not at the VPS — see `site/DEPLOYMENT.md`. Nothing here is live yet, and these prefixes are what let the two swap later without touching a URL anyone has.

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
