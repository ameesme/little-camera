import type { Db } from '../db.js';

export type SubscriberStatus = 'pending' | 'approved' | 'blocked';

export interface SubscriberRow {
  id: number;
  blog_id: number;
  email: string;
  name: string | null;
  status: SubscriberStatus;
  added_by: 'owner' | 'self';
  created_at: number;
  approved_at: number | null;
  last_notified_at: number | null;
}

export function findSubscriberById(db: Db, id: number): SubscriberRow | null {
  return (db.prepare('SELECT * FROM subscribers WHERE id = ?').get(id) as SubscriberRow | undefined) ?? null;
}

export function findSubscriberByEmail(db: Db, blogId: number, email: string): SubscriberRow | null {
  return (
    (db.prepare('SELECT * FROM subscribers WHERE blog_id = ? AND email = ?').get(blogId, email) as
      | SubscriberRow
      | undefined) ?? null
  );
}

export function listSubscribers(db: Db, blogId: number): SubscriberRow[] {
  return db.prepare('SELECT * FROM subscribers WHERE blog_id = ? ORDER BY id').all(blogId) as unknown as SubscriberRow[];
}

export function deleteSubscriber(db: Db, id: number): void {
  db.prepare('DELETE FROM subscribers WHERE id = ?').run(id);
}

export function listApprovedSubscribers(db: Db): SubscriberRow[] {
  return db.prepare(`SELECT * FROM subscribers WHERE status = 'approved' ORDER BY id`).all() as unknown as SubscriberRow[];
}

export function createSubscriber(
  db: Db,
  s: { blogId: number; email: string; name: string | null; status: SubscriberStatus; addedBy: 'owner' | 'self'; now: number },
): SubscriberRow {
  const approvedAt = s.status === 'approved' ? s.now : null;
  // Starting last_notified_at at approval keeps the first newsletter to photos posted after they joined.
  const r = db
    .prepare(
      `INSERT INTO subscribers (blog_id, email, name, status, added_by, created_at, approved_at, last_notified_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    )
    .run(s.blogId, s.email, s.name, s.status, s.addedBy, s.now, approvedAt, approvedAt);
  return findSubscriberById(db, Number(r.lastInsertRowid))!;
}

export function approveSubscriber(db: Db, id: number, now: number): void {
  db.prepare(
    `UPDATE subscribers SET status = 'approved', approved_at = ?, last_notified_at = COALESCE(last_notified_at, ?)
     WHERE id = ?`,
  ).run(now, now, id);
}

export function blockSubscriber(db: Db, id: number): void {
  db.prepare(`UPDATE subscribers SET status = 'blocked' WHERE id = ?`).run(id);
}

export function setSubscriberName(db: Db, id: number, name: string): void {
  db.prepare('UPDATE subscribers SET name = ? WHERE id = ?').run(name, id);
}

export function setLastNotified(db: Db, id: number, at: number): void {
  db.prepare('UPDATE subscribers SET last_notified_at = ? WHERE id = ?').run(at, id);
}
