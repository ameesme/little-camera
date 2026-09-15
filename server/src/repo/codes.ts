// Verification codes: the six characters inside the QR on /me.

import type { Db } from '../db.js';

export interface CodeRow {
  code: string;
  profile_id: number;
  created_at: number;
  expires_at: number;
  attempts: number;
}

/** 15 minutes, per protocol §5. */
export const CODE_TTL = 15 * 60;

export function insertCode(db: Db, code: string, profileId: number, now: number): CodeRow {
  db.prepare('INSERT INTO verification_codes (code, profile_id, created_at, expires_at) VALUES (?, ?, ?, ?)').run(
    code,
    profileId,
    now,
    now + CODE_TTL,
  );
  return findLiveCode(db, code, now)!;
}

export function findLiveCode(db: Db, code: string, now: number): CodeRow | null {
  return (
    (db.prepare('SELECT * FROM verification_codes WHERE code = ? AND expires_at > ?').get(code, now) as
      | CodeRow
      | undefined) ?? null
  );
}

/** Cheap gate before trying to decode a QR: is there anything to match against at all? */
export function anyLiveCode(db: Db, now: number): boolean {
  return db.prepare('SELECT 1 FROM verification_codes WHERE expires_at > ? LIMIT 1').get(now) !== undefined;
}

export function deleteCodesForProfile(db: Db, profileId: number): void {
  db.prepare('DELETE FROM verification_codes WHERE profile_id = ?').run(profileId);
}

export function deleteExpiredCodes(db: Db, now: number): number {
  return Number(db.prepare('DELETE FROM verification_codes WHERE expires_at <= ?').run(now).changes);
}
