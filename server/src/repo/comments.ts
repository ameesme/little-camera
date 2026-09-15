import type { Db } from '../db.js';

export interface CommentRow {
  id: number;
  photo_id: number;
  subscriber_id: number | null;
  body: string;
  created_at: number;
}

/** A comment with the name to print: the subscriber's name, or the owner's. */
export interface CommentView {
  id: number;
  photo_id: number;
  name: string;
  body: string;
}

export function insertComment(
  db: Db,
  c: { photoId: number; subscriberId: number | null; body: string; now: number },
): CommentRow {
  const r = db
    .prepare('INSERT INTO comments (photo_id, subscriber_id, body, created_at) VALUES (?, ?, ?, ?)')
    .run(c.photoId, c.subscriberId, c.body, c.now);
  return db.prepare('SELECT * FROM comments WHERE id = ?').get(Number(r.lastInsertRowid)) as unknown as CommentRow;
}

/** Comments for a set of photos in one query, oldest first, with display names resolved. */
export function commentsForPhotos(db: Db, photoIds: number[], ownerName: string): Map<number, CommentView[]> {
  const out = new Map<number, CommentView[]>();
  if (photoIds.length === 0) return out;
  const marks = photoIds.map(() => '?').join(',');
  const rows = db
    .prepare(
      `SELECT c.id, c.photo_id, c.body, s.name AS sub_name, c.subscriber_id
       FROM comments c LEFT JOIN subscribers s ON s.id = c.subscriber_id
       WHERE c.photo_id IN (${marks}) ORDER BY c.id`,
    )
    .all(...photoIds) as { id: number; photo_id: number; body: string; sub_name: string | null; subscriber_id: number | null }[];
  for (const r of rows) {
    const name = r.subscriber_id === null ? ownerName : (r.sub_name ?? 'Someone');
    const list = out.get(r.photo_id) ?? [];
    list.push({ id: r.id, photo_id: r.photo_id, name, body: r.body });
    out.set(r.photo_id, list);
  }
  return out;
}
