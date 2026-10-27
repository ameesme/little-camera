import { createHash } from 'node:crypto';
import { readFileSync, statSync } from 'node:fs';
import { basename, join } from 'node:path';
import { apiUrl, type Config } from '../config.js';

export const DEFAULT_HARDWARE = 'v3q';
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

export interface FirmwareState {
  installed: string | null;
  latest: ReturnType<typeof releaseJson> | null;
  update_available: boolean;
}

interface ManifestEntry {
  version?: unknown;
  channel?: unknown;
  hardware?: unknown;
  file?: unknown;
  notes?: unknown;
  released_at?: unknown;
}

const digests = new Map<string, { stamp: string; sha256: string }>();

function digestOf(path: string, size: number, mtimeMs: number): string {
  const stamp = `${size}:${mtimeMs}`;
  const cached = digests.get(path);
  if (cached?.stamp === stamp) return cached.sha256;
  const sha256 = createHash('sha256').update(readFileSync(path)).digest('hex');
  digests.set(path, { stamp, sha256 });
  return sha256;
}

const text = (v: unknown): string | null => (typeof v === 'string' && v.trim() !== '' ? v.trim() : null);

function readManifest(config: Config): ManifestEntry[] {
  let raw: string;
  try {
    raw = readFileSync(join(config.firmwareDir, 'manifest.json'), 'utf8');
  } catch {
    return [];
  }
  try {
    const parsed = JSON.parse(raw) as { releases?: unknown };
    return Array.isArray(parsed.releases) ? (parsed.releases as ManifestEntry[]) : [];
  } catch (err) {
    console.error(`firmware: manifest.json is not valid JSON (${(err as Error).message})`);
    return [];
  }
}

// One unreadable entry must not take the published ones down with it, so a bad entry is logged and dropped.
function toRelease(config: Config, entry: ManifestEntry): Release | null {
  const version = text(entry.version);
  const file = text(entry.file);
  if (!version || !VERSION.test(version) || !file) {
    console.error('firmware: skipping a manifest entry without a valid version and file');
    return null;
  }
  if (file !== basename(file)) {
    console.error(`firmware: ${version} names a path, not a file: ${file}`);
    return null;
  }
  const path = join(config.firmwareDir, file);
  let stat;
  try {
    stat = statSync(path);
    if (!stat.isFile()) throw new Error('not a file');
  } catch {
    console.error(`firmware: ${version} is in the manifest but ${file} is not on disk`);
    return null;
  }
  return {
    version,
    channel: text(entry.channel) ?? DEFAULT_CHANNEL,
    hardware: text(entry.hardware) ?? DEFAULT_HARDWARE,
    file,
    size: stat.size,
    // Measured, never taken from the manifest: this is what the camera checks the image against after the phone has carried it.
    sha256: digestOf(path, stat.size, stat.mtimeMs),
    notes: text(entry.notes),
    releasedAt: typeof entry.released_at === 'number' ? entry.released_at : null,
  };
}

export function listReleases(config: Config): Release[] {
  return readManifest(config)
    .map((entry) => toRelease(config, entry))
    .filter((r): r is Release => r !== null);
}

export function compareVersions(a: string, b: string): number {
  const left = a.split('.').map(Number);
  const right = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) {
    if ((left[i] ?? 0) !== (right[i] ?? 0)) return (left[i] ?? 0) < (right[i] ?? 0) ? -1 : 1;
  }
  return 0;
}

export function latestRelease(config: Config, opts: { hardware?: string; channel?: string } = {}): Release | null {
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
  return new Uint8Array(readFileSync(join(config.firmwareDir, release.file)));
}

export function releaseJson(config: Config, release: Release) {
  return {
    version: release.version,
    channel: release.channel,
    hardware: release.hardware,
    size: release.size,
    sha256: release.sha256,
    url: apiUrl(config, `/firmware/${release.hardware}/${release.version}.bin`),
    notes: release.notes,
    released_at: release.releasedAt,
  };
}

// The one place that decides whether an update is worth offering, so no client has to get version comparison right.
export function firmwareState(config: Config, installed: string | null, opts: { hardware?: string; channel?: string } = {}): FirmwareState {
  const release = latestRelease(config, opts);
  return {
    installed,
    latest: release ? releaseJson(config, release) : null,
    update_available: !!(release && installed && compareVersions(release.version, installed) > 0),
  };
}
