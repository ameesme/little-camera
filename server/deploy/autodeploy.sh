#!/usr/bin/env bash
# Pull-based deploy, run every minute on the VPS by little-camera-deploy.timer.
# Pull rather than push: the box needs no inbound SSH and GitHub holds no key to it.
# Fetches DEPLOY_BRANCH, fast-forwards the checkout, rebuilds only what the new commits touch and publishes a bumped firmware.
# FORCE=1 rebuilds the server and recreates Caddy even when nothing moved.
set -uo pipefail

REPO=${REPO:-/opt/little-camera}
BRANCH=${DEPLOY_BRANCH:-main}
STATE=${STATE_DIRECTORY:-/var/lib/little-camera-deploy}
FORCE=${FORCE:-0}
mkdir -p "$STATE/public"

cd "$REPO" || exit 1
git fetch -q origin "$BRANCH" || exit 1
target=$(git rev-parse FETCH_HEAD)

# Keyed on the last *attempt*, so a broken commit is tried once rather than rebuilt every minute.
if [ "$FORCE" != 1 ] && [ "$target" = "$(cat "$STATE/attempted" 2>/dev/null)" ]; then
    exit 0
fi
# Diffed from the last *good* deploy, so a fix after a failure still rebuilds what the failure left undone.
from=$(cat "$STATE/deployed" 2>/dev/null || git rev-parse HEAD)
echo "$target" > "$STATE/attempted"

deploy() {
    if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
        echo "refusing: the checkout has local edits"
        git status --short --untracked-files=no
        return 1
    fi
    git merge -q --ff-only "$target" || { echo "refusing: $BRANCH is not a fast-forward of the checkout"; return 1; }
    echo "at $(git log -1 --format='%h %s')"

    local changed code server=0 caddy=0
    changed=$(git diff --name-only "$from" "$target")
    code=$(grep -Ev '^server/deploy/|\.md$' <<<"$changed")
    # What server/Dockerfile copies; site/ is a live bind mount and needs nothing.
    grep -Eq '^(server/|packages/|package\.json$|pnpm-lock\.yaml$|pnpm-workspace\.yaml$)' <<<"$code" && server=1
    grep -Eq '^server/deploy/(Caddyfile|Dockerfile\.caddy)$' <<<"$changed" && caddy=1
    if [ "$FORCE" = 1 ] || grep -q '^server/deploy/docker-compose\.yml$' <<<"$changed"; then
        server=1 caddy=1
    fi

    cd server/deploy || return 1
    if [ "$server" = 1 ]; then
        echo "rebuilding server"
        docker compose up -d --build server || return 1
    fi
    # Recreated, not reloaded: git replaces the Caddyfile with a new inode, and a single-file bind mount keeps the old one.
    if [ "$caddy" = 1 ]; then
        echo "recreating caddy"
        docker compose up -d --no-deps --force-recreate caddy || return 1
    fi
    if [ "$server" = 1 ]; then
        wait_for_server || return 1
    fi
    release_firmware
}

wait_for_server() {
    for _ in $(seq 30); do
        if docker compose exec -T server node -e \
            'fetch(`http://localhost:3000/caddy/ask?domain=${process.env.BASE_DOMAIN}`).then(r => process.exit(r.ok ? 0 : 1), () => process.exit(1))' \
            2>/dev/null; then
            echo "server answering"
            return 0
        fi
        sleep 2
    done
    echo "server not answering after 60s"
    docker compose logs --tail=30 server
    return 1
}

# Bumping version.h on the branch is the release: a version the manifest lacks is built here and published.
release_firmware() {
    local version file notes known
    version=$(sed -nE 's/^#define LC_VERSION_(MAJOR|MINOR|PATCH) ([0-9]+)$/\2/p' "$REPO/firmware/src/version.h" | paste -sd. -)
    docker compose exec -T server node -e '
        const fs = require("fs"), p = "/data/firmware/manifest.json";
        const m = fs.existsSync(p) ? JSON.parse(fs.readFileSync(p, "utf8")) : { releases: [] };
        process.exit(m.releases.some(r => r.version === process.argv[1]) ? 0 : 2);' "$version"
    known=$?
    [ "$known" = 0 ] && { echo "firmware $version already published"; return 0; }
    [ "$known" = 2 ] || { echo "cannot read the firmware manifest"; return 1; }

    echo "building firmware $version"
    # The toolchain lives in a named volume: the first build downloads it, every later one reuses it.
    docker run --rm -v "$REPO/firmware:/fw" -v little-camera-pio:/cache -e PLATFORMIO_CORE_DIR=/cache/core -w /fw python:3.12-slim \
        sh -c '[ -x /cache/venv/bin/pio ] || { python -m venv /cache/venv && /cache/venv/bin/pip install -q platformio; } && /cache/venv/bin/pio run -e v3q' \
        || return 1

    file="little-camera-$version.bin"
    notes=$(git -C "$REPO" log -1 --format=%s -- firmware/src/version.h | sed -E 's/^[a-z]+(\([^)]*\))?: //')
    # The image goes in before the manifest names it, so a camera is never offered a file that is not there.
    docker compose cp "$REPO/firmware/.pio/build/v3q/firmware.bin" "server:/data/firmware/$file" || return 1
    docker compose exec -T server node -e '
        const fs = require("fs"), p = "/data/firmware/manifest.json", [, version, file, notes] = process.argv;
        const m = fs.existsSync(p) ? JSON.parse(fs.readFileSync(p, "utf8")) : { releases: [] };
        m.releases.unshift({ version, channel: "stable", hardware: "v3q", file, notes, released_at: Math.floor(Date.now() / 1000) });
        fs.writeFileSync(p + ".tmp", JSON.stringify(m, null, 2) + "\n");
        fs.renameSync(p + ".tmp", p);' "$version" "$file" "$notes" || return 1
    echo "published firmware $version: $notes"
}

deploy > "$STATE/last.log" 2>&1
code=$?
[ "$code" = 0 ] && echo "$target" > "$STATE/deployed"

# Public at https://<DOMAIN>/api/deploy-status: only this script's own output and the build's, no environment.
{
    echo "result: $([ "$code" = 0 ] && echo ok || echo failed)"
    echo "commit: $target"
    echo "branch: $BRANCH"
    echo "at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo
    tail -n 40 "$STATE/last.log"
} > "$STATE/public/status.tmp" && mv "$STATE/public/status.tmp" "$STATE/public/status.txt"

cat "$STATE/last.log"
exit "$code"
