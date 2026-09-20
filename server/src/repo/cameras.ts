import type { Db } from '../db.js';

export interface CameraRow {
  id: string;
  secret_hash: string;
  short_code: string;
  profile_id: number | null;
  bound_at: number | null;
  first_seen: number;
  last_seen: number;
  /** What the bridge app last reported from Info, or null from a camera too old to say. */
  firmware_version: string | null;
  firmware_seen_at: number | null;
}

export function findCamera(db: Db, id: string): CameraRow | null {
  return (db.prepare('SELECT * FROM cameras WHERE id = ?').get(id) as CameraRow | undefined) ?? null;
}

export function findCameraByShortCode(db: Db, code: string): CameraRow | null {
  return (db.prepare('SELECT * FROM cameras WHERE short_code = ?').get(code) as CameraRow | undefined) ?? null;
}

/** The camera bound to a profile (a profile has at most one in practice; take the newest binding). */
export function findCameraByProfile(db: Db, profileId: number): CameraRow | null {
  return (
    (db
      .prepare('SELECT * FROM cameras WHERE profile_id = ? ORDER BY bound_at DESC LIMIT 1')
      .get(profileId) as CameraRow | undefined) ?? null
  );
}

export function createCamera(db: Db, c: { id: string; secretHash: string; shortCode: string; now: number }): CameraRow {
  db.prepare(
    'INSERT INTO cameras (id, secret_hash, short_code, first_seen, last_seen) VALUES (?, ?, ?, ?, ?)',
  ).run(c.id, c.secretHash, c.shortCode, c.now, c.now);
  return findCamera(db, c.id)!;
}

export function touchCamera(db: Db, id: string, now: number): void {
  db.prepare('UPDATE cameras SET last_seen = ? WHERE id = ?').run(now, id);
}

/** Record the firmware the phone read out of Info. Null versions are not stored over a known one. */
export function setCameraFirmware(db: Db, id: string, version: string, now: number): void {
  db.prepare('UPDATE cameras SET firmware_version = ?, firmware_seen_at = ? WHERE id = ?').run(version, now, id);
}

/** Bind a camera to a profile and move its orphan photos across. */
export function bindCamera(db: Db, cameraId: string, profileId: number, now: number): void {
  db.prepare('UPDATE cameras SET profile_id = ?, bound_at = ? WHERE id = ?').run(profileId, now, cameraId);
  db.prepare('UPDATE photos SET profile_id = ? WHERE camera_id = ? AND profile_id IS NULL').run(profileId, cameraId);
}
