// Firmware releases (protocol §4.7).
//
// A release is a file on disk plus a line in a manifest next to it — not a
// database row. Publishing is `scp` and an edit; rolling back is deleting a
// line. Nothing here is per-camera state, so the database would only be a
// place for it to get out of step with the files.
//
// The manifest never states a size or a digest: both are read from the file
// itself, so a stale manifest cannot make the server promise bytes it is not
// serving. The digest is what the camera checks the image against after the
// phone has carried it (protocol §3.7), which is the reason it has to be the
// one thing nobody types in.
//
//   <FIRMWARE_DIR>/manifest.json
//   <FIRMWARE_DIR>/little-camera-0.2.0.bin
//
//   {
//     "releases": [
//       {
//         "version": "0.2.0",
//         "channel": "stable",
//         "hardware": "xiao_shutter",
//         "file": "little-camera-0.2.0.bin",
//         "notes": "Firmware update over BLE.",
//         "released_at": 1757789000
//       }
//     ]
//   }

import { createHash } from 'node:crypto';
import { readFileSync, statSync } from 'node:fs';
import { basename, join } from 'node:path';
import type { Config } from '../config.js';
import { apiUrl } from '../config.js';

export const DEFAULT_HARDWARE = 'xiao_shutter';
export const DEFAULT_CHANNEL = 'stable';
export const VERSION = /^\d{1,3}\.\d{1,3}\.\d{1,3}$/;

export interface Release {
  version: string;
  channel: string;
  hardware: string;
  file: string;
  size: number;
  sha256: string;
  notes: string | null;
  releasedAt: number | null;
}

interface ManifestEntry {
  version?: unknown;
  channel?: unknown;
  hardware?: unknown;
  file?: unknown;
  notes?: unknown;
  released_at?: unknown;
}

/** Hashing 1.5 MB per request would be silly; the file's identity is its path, size and mtime. */
const digests = new Map<string, { key: string; sha256: string }>();

function digestOf(path: string, size: number, mtimeMs: number): string {
  const key = `${size}:${mtimeMs}`;
  const cached = digests.get(path);
  if (cached && cached.key === key) return cached.sha256;
  const sha256 = createHash('sha256').update(readFileSync(path)).digest('hex');
  digests.set(path, { key, sha256 });
  return sha256;
}

const str = (v: unknown): string | null => (typeof v === 'string' && v.trim() !== '' ? v.trim() : null);

/**
 * Every release the manifest describes and the disk backs up. Anything that
 * does not parse, or whose file is missing, is dropped with a line in the log
 * rather than failing the request: one bad entry must not take the others out.
 */
export function listReleases(config: Config): Release[] {
  let raw: string;
  try {
    raw = readFileSync(join(config.firmwareDir, 'manifest.json'), 'utf8');
  } catch {
    return [];  // No manifest at all is the normal state of a fresh install.
  }
  let entries: ManifestEntry[];
  try {
    const parsed = JSON.parse(raw) as { releases?: unknown };
    entries = Array.isArray(parsed.releases) ? (parsed.releases as ManifestEntry[]) : [];
  } catch (err) {
    console.error(`firmware: manifest.json is not valid JSON (${(err as Error).message})`);
    return [];
  }

  const releases: Release[] = [];
  for (const entry of entries) {
    const version = str(entry.version);
    const file = str(entry.file);
    if (!version || !VERSION.test(version) || !file) {
      console.error(`firmware: skipping a manifest entry without a valid version and file`);
      continue;
    }
    // The manifest is written by whoever deploys, but a path out of the
    // firmware directory would still be a file read this route can be talked
    // into. Names only.
    if (file !== basename(file)) {
      console.error(`firmware: ${version} names a path, not a file: ${file}`);
      continue;
    }
    const path = join(config.firmwareDir, file);
    let size: number;
    let mtimeMs: number;
    try {
      const st = statSync(path);
      if (!st.isFile()) throw new Error('not a file');
      size = st.size;
      mtimeMs = st.mtimeMs;
    } catch {
      console.error(`firmware: ${version} is in the manifest but ${file} is not on disk`);
      continue;
    }
    releases.push({
      version,
      channel: str(entry.channel) ?? DEFAULT_CHANNEL,
      hardware: str(entry.hardware) ?? DEFAULT_HARDWARE,
      file,
      size,
      sha256: digestOf(path, size, mtimeMs),
      notes: str(entry.notes),
      releasedAt: typeof entry.released_at === 'number' ? entry.released_at : null,
    });
  }
  return releases;
}

/** `0.10.0` is newer than `0.9.9`: dotted numbers, not strings. */
export function compareVersions(a: string, b: string): number {
  const pa = a.split('.').map(Number);
  const pb = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) {
    if ((pa[i] ?? 0) !== (pb[i] ?? 0)) return (pa[i] ?? 0) < (pb[i] ?? 0) ? -1 : 1;
  }
  return 0;
}

export function latestRelease(
  config: Config,
  opts: { hardware?: string; channel?: string } = {},
): Release | null {
  const hardware = opts.hardware ?? DEFAULT_HARDWARE;
  const channel = opts.channel ?? DEFAULT_CHANNEL;
  const matching = listReleases(config).filter((r) => r.hardware === hardware && r.channel === channel);
  if (matching.length === 0) return null;
  return matching.reduce((best, r) => (compareVersions(r.version, best.version) > 0 ? r : best));
}

export function findRelease(config: Config, hardware: string, version: string): Release | null {
  return listReleases(config).find((r) => r.hardware === hardware && r.version === version) ?? null;
}

export function releaseBytes(config: Config, release: Release): Uint8Array<ArrayBuffer> {
  // Read per request rather than cached in memory: an image is 1.5 MB, a
  // camera owner updates a handful of times a year, and the alternative is a
  // cache that has to be invalidated when someone replaces the file.
  return new Uint8Array(readFileSync(join(config.firmwareDir, release.file)));
}

export function releaseUrl(config: Config, release: Release): string {
  return apiUrl(config, `/firmware/${release.hardware}/${release.version}.bin`);
}

/** The JSON shape both `/api/firmware/latest` and `/api/camera/status` hand out. */
export function releaseJson(config: Config, release: Release) {
  return {
    version: release.version,
    channel: release.channel,
    hardware: release.hardware,
    size: release.size,
    sha256: release.sha256,
    url: releaseUrl(config, release),
    notes: release.notes,
    released_at: release.releasedAt,
  };
}
