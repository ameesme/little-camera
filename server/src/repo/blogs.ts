import type { Db } from '../db.js';

export interface BlogRow {
  id: number;
  handle: string;
  name: string;
  email: string;
  email_verified_at: number | null;
  avatar_photo_id: number | null;
  avatar_requested_at: number | null;
  created_at: number;
}

export function findBlogById(db: Db, id: number): BlogRow | null {
  return (db.prepare('SELECT * FROM blogs WHERE id = ?').get(id) as BlogRow | undefined) ?? null;
}

export function findBlogByHandle(db: Db, handle: string): BlogRow | null {
  return (db.prepare('SELECT * FROM blogs WHERE handle = ?').get(handle) as BlogRow | undefined) ?? null;
}

export function findBlogByEmail(db: Db, email: string): BlogRow | null {
  return (db.prepare('SELECT * FROM blogs WHERE email = ?').get(email) as BlogRow | undefined) ?? null;
}

export function createBlog(db: Db, p: { handle: string; name: string; email: string; now: number }): BlogRow {
  const r = db
    .prepare('INSERT INTO blogs (handle, name, email, created_at) VALUES (?, ?, ?, ?)')
    .run(p.handle, p.name, p.email, p.now);
  return findBlogById(db, Number(r.lastInsertRowid))!;
}

export function markEmailVerified(db: Db, id: number, now: number): void {
  db.prepare('UPDATE blogs SET email_verified_at = COALESCE(email_verified_at, ?) WHERE id = ?').run(now, id);
}

export function setAvatarRequested(db: Db, id: number, at: number | null): void {
  db.prepare('UPDATE blogs SET avatar_requested_at = ? WHERE id = ?').run(at, id);
}

export function setAvatarPhoto(db: Db, id: number, photoId: number): void {
  db.prepare('UPDATE blogs SET avatar_photo_id = ?, avatar_requested_at = NULL WHERE id = ?').run(photoId, id);
}
