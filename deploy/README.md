# Deploying the micro-blog

One VPS, Docker Compose, two containers: the Node server (SQLite, photos and
firmware images on a volume) and Caddy in front with a wildcard certificate.

The apex is shared. Caddy forwards `/api/*` and `/app/*` to the server and
serves everything else — `/` above all — from `LANDING_DIR`, so the landing
page and this app can be built and deployed apart from each other. Blog
subdomains go straight through; each blog owns the root of its own subdomain.

```
https://lttl.cam/            landing page (static files, LANDING_DIR)
https://lttl.cam/app/…       registration, sign-in, /app/me
https://lttl.cam/api/…       the camera API and firmware releases
https://mees.lttl.cam/       a blog
```

1. Point DNS at the box: `A  lttl.cam  → IP` and `A  *.lttl.cam → IP`.
2. `cp .env.example .env` and fill it in. The wildcard certificate needs the
   DNS-01 challenge; the shipped Caddy build includes the Cloudflare module,
   so a Cloudflare API token with `Zone:Read` + `DNS:Edit` on that zone is the
   only extra secret. Other providers: edit `Dockerfile.caddy` and the `tls`
   blocks in `Caddyfile`.
3. `docker compose up -d --build`.
4. Open `https://lttl.cam/app/register`.

## Publishing a firmware release

Firmware images are files on the data volume with a manifest beside them
(`docs/protocol.md` §4.7); nothing about a release is in the database, so
publishing one is a copy and an edit, and unpublishing is deleting a line.

```
pio run -e xiao_shutter                     # on your machine
docker compose cp .pio/build/xiao_shutter/firmware.bin \
    server:/data/firmware/little-camera-0.2.0.bin
docker compose exec server sh -c 'cat >> /data/firmware/manifest.json' # or edit it
```

```json
{
  "releases": [
    {
      "version": "0.2.0",
      "channel": "stable",
      "hardware": "xiao_shutter",
      "file": "little-camera-0.2.0.bin",
      "notes": "Firmware update over BLE.",
      "released_at": 1757789000
    }
  ]
}
```

The size and the SHA-256 are read from the file itself, never from the
manifest — the digest is what the camera checks the image against after the
phone has carried it, so it must not be something anybody types. Check it with
`curl https://lttl.cam/api/firmware/latest`.

Backups: the `data` volume holds `little-camera.sqlite` (SQLite, WAL mode),
`photos/` and `firmware/`.
`docker compose exec server sqlite3 /data/little-camera.sqlite ".backup /data/backup.db"`
or just copy the volume while the container is stopped.

Local development needs none of this; see `server/README.md`.
