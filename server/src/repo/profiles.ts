import type { Db } from '../db.js';

export interface ProfileRow {
  id: number;
  handle: string;
  name: string;
  email: string;
  email_verified_at: number | null;
  avatar_photo_id: number | null;
  avatar_requested_at: number | null;
  created_at: number;
}

export function findProfileById(db: Db, id: number): ProfileRow | null {
  return (db.prepare('SELECT * FROM profiles WHERE id = ?').get(id) as ProfileRow | undefined) ?? null;
}

export function findProfileByHandle(db: Db, handle: string): ProfileRow | null {
  return (db.prepare('SELECT * FROM profiles WHERE handle = ?').get(handle) as ProfileRow | undefined) ?? null;
}

export function findProfileByEmail(db: Db, email: string): ProfileRow | null {
  return (db.prepare('SELECT * FROM profiles WHERE email = ?').get(email) as ProfileRow | undefined) ?? null;
}

export function createProfile(db: Db, p: { handle: string; name: string; email: string; now: number }): ProfileRow {
  const r = db
    .prepare('INSERT INTO profiles (handle, name, email, created_at) VALUES (?, ?, ?, ?)')
    .run(p.handle, p.name, p.email, p.now);
  return findProfileById(db, Number(r.lastInsertRowid))!;
}

export function markEmailVerified(db: Db, id: number, now: number): void {
  db.prepare('UPDATE profiles SET email_verified_at = COALESCE(email_verified_at, ?) WHERE id = ?').run(now, id);
}

export function setAvatarRequested(db: Db, id: number, at: number | null): void {
  db.prepare('UPDATE profiles SET avatar_requested_at = ? WHERE id = ?').run(at, id);
}

export function setAvatarPhoto(db: Db, id: number, photoId: number): void {
  db.prepare('UPDATE profiles SET avatar_photo_id = ?, avatar_requested_at = NULL WHERE id = ?').run(photoId, id);
}
