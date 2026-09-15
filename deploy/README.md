# Deploying the micro-blog

One VPS, Docker Compose, two containers: the Node server (SQLite + photos on a
volume) and Caddy in front with a wildcard certificate.

1. Point DNS at the box: `A  yourdomain  → IP` and `A  *.yourdomain → IP`.
2. `cp .env.example .env` and fill it in. The wildcard certificate needs the
   DNS-01 challenge; the shipped Caddy build includes the Cloudflare module,
   so a Cloudflare API token with `Zone:Read` + `DNS:Edit` on that zone is the
   only extra secret. Other providers: edit `Dockerfile.caddy` and the `tls`
   block in `Caddyfile`.
3. `docker compose up -d --build`.
4. Open `https://yourdomain/register`.

Backups: the `data` volume holds `app.db` (SQLite, WAL mode) and `photos/`.
`docker compose exec server sqlite3 /data/app.db ".backup /data/backup.db"` or
just copy the volume while the container is stopped.

Local development needs none of this; see `server/README.md`.
