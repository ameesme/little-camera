import type { Db } from '../db.js';

export type SubscriberStatus = 'pending' | 'approved' | 'blocked';

export interface SubscriberRow {
  id: number;
  profile_id: number;
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

export function findSubscriberByEmail(db: Db, profileId: number, email: string): SubscriberRow | null {
  return (
    (db.prepare('SELECT * FROM subscribers WHERE profile_id = ? AND email = ?').get(profileId, email) as
      | SubscriberRow
      | undefined) ?? null
  );
}

export function listSubscribers(db: Db, profileId: number): SubscriberRow[] {
  return db.prepare('SELECT * FROM subscribers WHERE profile_id = ? ORDER BY id').all(profileId) as unknown as SubscriberRow[];
}

export function listApprovedSubscribers(db: Db): SubscriberRow[] {
  return db.prepare(`SELECT * FROM subscribers WHERE status = 'approved' ORDER BY id`).all() as unknown as SubscriberRow[];
}

export function createSubscriber(
  db: Db,
  s: { profileId: number; email: string; name: string | null; status: SubscriberStatus; addedBy: 'owner' | 'self'; now: number },
): SubscriberRow {
  const approvedAt = s.status === 'approved' ? s.now : null;
  // last_notified_at starts at approval so the newsletter only covers
  // photos posted after someone joined, not the whole archive.
  const r = db
    .prepare(
      `INSERT INTO subscribers (profile_id, email, name, status, added_by, created_at, approved_at, last_notified_at)
       VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
    )
    .run(s.profileId, s.email, s.name, s.status, s.addedBy, s.now, approvedAt, approvedAt);
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
