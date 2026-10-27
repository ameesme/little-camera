import type { Db } from '../db.js';

export type PhotoKind = 'photo' | 'verification' | 'avatar';
export type CapturedAtSource = 'camera' | 'phone' | 'upload';

export interface PhotoRow {
  id: number;
  public_id: string;
  camera_id: string;
  blog_id: number | null;
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
      `INSERT INTO photos (public_id, camera_id, blog_id, kind, sha256, path, camera_index,
                           captured_at, captured_at_source, uploaded_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)`,
    )
    .run(
      p.public_id,
      p.camera_id,
      p.blog_id,
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

/** Protocol §2: a batch would otherwise share one second, so each earlier index is pushed a second back instead. */
export function shiftUploadBatchBack(db: Db, cameraId: string, cameraIndex: number, now: number): void {
  db.prepare(
    `UPDATE photos SET captured_at = captured_at - 1
     WHERE camera_id = ? AND captured_at_source = 'upload' AND camera_index < ? AND uploaded_at >= ?`,
  ).run(cameraId, cameraIndex, now - 120);
}

/** Posts visible on a blog: photos and avatars, never verification shots. */
export const FEED_KINDS_SQL = `kind IN ('photo', 'avatar')`;

export function countFeedPhotos(db: Db, blogId: number): number {
  const r = db
    .prepare(`SELECT COUNT(*) AS n FROM photos WHERE blog_id = ? AND ${FEED_KINDS_SQL}`)
    .get(blogId) as { n: number };
  return r.n;
}

export function newestFeedPhoto(db: Db, blogId: number): PhotoRow | null {
  return (
    (db
      .prepare(
        `SELECT * FROM photos WHERE blog_id = ? AND ${FEED_KINDS_SQL} ORDER BY uploaded_at DESC, id DESC LIMIT 1`,
      )
      .get(blogId) as PhotoRow | undefined) ?? null
  );
}

/** Newest first by upload time, not capture time, so a photo synced late still arrives at the top. */
export function feedPage(
  db: Db,
  blogId: number,
  limit: number,
  before?: { uploadedAt: number; id: number },
): PhotoRow[] {
  if (before) {
    return db
      .prepare(
        `SELECT * FROM photos
         WHERE blog_id = ? AND ${FEED_KINDS_SQL}
           AND (uploaded_at < ? OR (uploaded_at = ? AND id < ?))
         ORDER BY uploaded_at DESC, id DESC LIMIT ?`,
      )
      .all(blogId, before.uploadedAt, before.uploadedAt, before.id, limit) as unknown as PhotoRow[];
  }
  return db
    .prepare(
      `SELECT * FROM photos WHERE blog_id = ? AND ${FEED_KINDS_SQL}
       ORDER BY uploaded_at DESC, id DESC LIMIT ?`,
    )
    .all(blogId, limit) as unknown as PhotoRow[];
}

/** Feed photos newer than `since` (upload time), oldest first. Used by the newsletter. */
export function feedPhotosSince(db: Db, blogId: number, since: number): PhotoRow[] {
  return db
    .prepare(
      `SELECT * FROM photos WHERE blog_id = ? AND ${FEED_KINDS_SQL} AND uploaded_at > ?
       ORDER BY uploaded_at ASC, id ASC`,
    )
    .all(blogId, since) as unknown as PhotoRow[];
}

/** Photos from cameras nobody has claimed, uploaded before `cutoff`. */
export function orphanPhotosBefore(db: Db, cutoff: number): PhotoRow[] {
  return db
    .prepare('SELECT * FROM photos WHERE blog_id IS NULL AND uploaded_at < ?')
    .all(cutoff) as unknown as PhotoRow[];
}

export function deletePhoto(db: Db, id: number): void {
  db.prepare('DELETE FROM photos WHERE id = ?').run(id);
}
