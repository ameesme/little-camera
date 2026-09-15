import type { Db } from '../db.js';

export type PhotoKind = 'photo' | 'verification' | 'avatar';
export type CapturedAtSource = 'camera' | 'phone' | 'upload';

export interface PhotoRow {
  id: number;
  public_id: string;
  camera_id: string;
  profile_id: number | null;
  kind: PhotoKind;
  sha256: string;
  path: string;
  camera_index: number;
  captured_at: number;
  captured_at_source: CapturedAtSource;
  uploaded_at: number;
}

export function findPhotoById(db: Db, id: number): PhotoRow | null {
  return (db.prepare('SELECT * FROM photos WHERE id = ?').get(id) as PhotoRow | undefined) ?? null;
}

export function findPhotoByPublicId(db: Db, publicId: string): PhotoRow | null {
  return (db.prepare('SELECT * FROM photos WHERE public_id = ?').get(publicId) as PhotoRow | undefined) ?? null;
}

export function findPhotoByHash(db: Db, cameraId: string, sha256: string): PhotoRow | null {
  return (
    (db.prepare('SELECT * FROM photos WHERE camera_id = ? AND sha256 = ?').get(cameraId, sha256) as
      | PhotoRow
      | undefined) ?? null
  );
}

export function insertPhoto(
  db: Db,
  p: Omit<PhotoRow, 'id'>,
): PhotoRow {
  const r = db
    .prepare(
      `INSERT INTO photos (public_id, camera_id, profile_id, kind, sha256, path, camera_index,
                           captured_at, captured_at_source, uploaded_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    )
    .run(
      p.public_id,
      p.camera_id,
      p.profile_id,
      p.kind,
      p.sha256,
      p.path,
      p.camera_index,
      p.captured_at,
      p.captured_at_source,
      p.uploaded_at,
    );
  return findPhotoById(db, Number(r.lastInsertRowid))!;
}

export function setPhotoKind(db: Db, id: number, kind: PhotoKind): void {
  db.prepare('UPDATE photos SET kind = ? WHERE id = ?').run(kind, id);
}

/**
 * Batch-ordering tweak from protocol §2: when several upload-dated photos
 * from one camera arrive together they would all get the same second, so
 * before dating a new one at `now` we push the earlier ones of the batch
 * back by a second. Lowest camera_index ends up oldest, nothing is dated
 * in the future. "Together" = uploaded in the last two minutes.
 */
export function shiftUploadBatchBack(db: Db, cameraId: string, cameraIndex: number, now: number): void {
  db.prepare(
    `UPDATE photos SET captured_at = captured_at - 1
     WHERE camera_id = ? AND captured_at_source = 'upload' AND camera_index < ? AND uploaded_at >= ?`,
  ).run(cameraId, cameraIndex, now - 120);
}

/** Posts visible on a blog: photos and avatars, never verification shots. */
export const FEED_KINDS_SQL = `kind IN ('photo', 'avatar')`;

export function countFeedPhotos(db: Db, profileId: number): number {
  const r = db
    .prepare(`SELECT COUNT(*) AS n FROM photos WHERE profile_id = ? AND ${FEED_KINDS_SQL}`)
    .get(profileId) as { n: number };
  return r.n;
}

export function newestFeedPhoto(db: Db, profileId: number): PhotoRow | null {
  return (
    (db
      .prepare(
        `SELECT * FROM photos WHERE profile_id = ? AND ${FEED_KINDS_SQL} ORDER BY uploaded_at DESC, id DESC LIMIT 1`,
      )
      .get(profileId) as PhotoRow | undefined) ?? null
  );
}

/**
 * One page of the feed, newest first. `before` is a (uploaded_at, id) pair
 * from the last item of the previous page. Ordering by upload time (not
 * capture time) means a photo synced late still shows up at the top, which
 * is what a subscriber expects from a "new pictures" email.
 */
export function feedPage(
  db: Db,
  profileId: number,
  limit: number,
  before?: { uploadedAt: number; id: number },
): PhotoRow[] {
  if (before) {
    return db
      .prepare(
        `SELECT * FROM photos
         WHERE profile_id = ? AND ${FEED_KINDS_SQL}
           AND (uploaded_at < ? OR (uploaded_at = ? AND id < ?))
         ORDER BY uploaded_at DESC, id DESC LIMIT ?`,
      )
      .all(profileId, before.uploadedAt, before.uploadedAt, before.id, limit) as unknown as PhotoRow[];
  }
  return db
    .prepare(
      `SELECT * FROM photos WHERE profile_id = ? AND ${FEED_KINDS_SQL}
       ORDER BY uploaded_at DESC, id DESC LIMIT ?`,
    )
    .all(profileId, limit) as unknown as PhotoRow[];
}

/** Feed photos newer than `since` (upload time), oldest first. Used by the newsletter. */
export function feedPhotosSince(db: Db, profileId: number, since: number): PhotoRow[] {
  return db
    .prepare(
      `SELECT * FROM photos WHERE profile_id = ? AND ${FEED_KINDS_SQL} AND uploaded_at > ?
       ORDER BY uploaded_at ASC, id ASC`,
    )
    .all(profileId, since) as unknown as PhotoRow[];
}

/** Photos from cameras nobody has claimed, uploaded before `cutoff`. */
export function orphanPhotosBefore(db: Db, cutoff: number): PhotoRow[] {
  return db
    .prepare('SELECT * FROM photos WHERE profile_id IS NULL AND uploaded_at < ?')
    .all(cutoff) as unknown as PhotoRow[];
}

export function deletePhoto(db: Db, id: number): void {
  db.prepare('DELETE FROM photos WHERE id = ?').run(id);
}

/** When this camera last uploaded anything, or null. Proof of possession for the typed-code fallback. */
export function lastUploadAt(db: Db, cameraId: string): number | null {
  const row = db.prepare('SELECT MAX(uploaded_at) AS t FROM photos WHERE camera_id = ?').get(cameraId) as { t: number | null };
  return row.t ?? null;
}
