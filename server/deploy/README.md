# Deploying the micro-blog

One VPS, Docker Compose, two containers: the Node server, with SQLite and the photos on a volume, and Caddy in front holding a wildcard certificate.

1. Point DNS at the box: `A  yourdomain  → IP` and `A  *.yourdomain → IP`.
2. `cp .env.example .env` and fill it in. The wildcard certificate needs the DNS-01 challenge, and the Caddy build here ships the Cloudflare module, so the only extra secret is a Cloudflare API token with `Zone:Read` + `DNS:Edit` on that zone. For another provider, edit `Dockerfile.caddy` and the `tls` block in `Caddyfile`.
3. `docker compose up -d --build`.
4. Open `https://yourdomain/register`.

Backups: the `data` volume holds `app.db` (SQLite, WAL mode) and `photos/`. Either run `docker compose exec server sqlite3 /data/app.db ".backup /data/backup.db"` or copy the volume while the container is stopped.

Local development needs none of this, see `server/README.md`.
