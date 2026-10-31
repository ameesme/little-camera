import type { Db } from '../db.js';

export interface CameraRow {
  id: string;
  secret_hash: string;
  short_code: string;
  blog_id: number | null;
  bound_at: number | null;
  first_seen: number;
  last_seen: number;
  /** What the bridge app last reported from Info, or null from a camera too old to say. */
  firmware_version: string | null;
  firmware_seen_at: number | null;
  battery_percent: number | null;
  battery_mv: number | null;
  charging: number;
  used_bytes: number | null;
  total_bytes: number | null;
  happiness: number | null;
  mood_elapsed_s: number | null;
  telemetry_at: number | null;
  claim_approved_at: number | null;
  claim_blog_id: number | null;
  verified_email: string | null;
}

export interface Telemetry {
  batteryPercent: number;
  batteryMv: number;
  charging: boolean;
  usedBytes: number;
  totalBytes: number;
  happiness: number;
  moodElapsedS: number;
}

/** What the phone read out of Info. Stored as reported: the percentage is the camera's, not ours. */
export function setCameraTelemetry(db: Db, id: string, t: Telemetry, now: number): void {
  db.prepare(
    `UPDATE cameras SET battery_percent = ?, battery_mv = ?, charging = ?, used_bytes = ?,
     total_bytes = ?, happiness = ?, mood_elapsed_s = ?, telemetry_at = ? WHERE id = ?`,
  ).run(t.batteryPercent, t.batteryMv, t.charging ? 1 : 0, t.usedBytes, t.totalBytes, t.happiness, t.moodElapsedS, now, id);
}

export function approveCameraClaim(db: Db, id: string, blogId: number, now: number): void {
  db.prepare('UPDATE cameras SET claim_approved_at = ?, claim_blog_id = ? WHERE id = ?').run(now, blogId, id);
}

export function setCameraVerifiedEmail(db: Db, id: string, email: string | null): void {
  db.prepare('UPDATE cameras SET verified_email = ? WHERE id = ?').run(email, id);
}

export function findCamera(db: Db, id: string): CameraRow | null {
  return (db.prepare('SELECT * FROM cameras WHERE id = ?').get(id) as CameraRow | undefined) ?? null;
}

/** The camera bound to a blog (a blog has at most one in practice; take the newest binding). */
export function findCameraByBlog(db: Db, blogId: number): CameraRow | null {
  return (
    (db
      .prepare('SELECT * FROM cameras WHERE blog_id = ? ORDER BY bound_at DESC LIMIT 1')
      .get(blogId) as CameraRow | undefined) ?? null
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

/** Bind a camera to a blog and move its orphan photos across. */
export function bindCamera(db: Db, cameraId: string, blogId: number, now: number): void {
  db.prepare('UPDATE cameras SET blog_id = ?, bound_at = ? WHERE id = ?').run(blogId, now, cameraId);
  db.prepare('UPDATE photos SET blog_id = ? WHERE camera_id = ? AND blog_id IS NULL').run(blogId, cameraId);
}
