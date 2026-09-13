import type { Db } from '../db.js';

export type TokenKind = 'subscriber' | 'owner_login' | 'email_verify';

export interface TokenRow {
  id: number;
  token_hash: string;
  kind: TokenKind;
  profile_id: number;
  subscriber_id: number | null;
  photo_id: number | null;
  expires_at: number;
  first_used_at: number | null;
  created_at: number;
}

export function insertToken(
  db: Db,
  t: { tokenHash: string; kind: TokenKind; profileId: number; subscriberId: number | null; photoId: number | null; expiresAt: number; now: number },
): TokenRow {
  const r = db
    .prepare(
      `INSERT INTO access_tokens (token_hash, kind, profile_id, subscriber_id, photo_id, expires_at, created_at)
       VALUES (?, ?, ?, ?, ?, ?, ?)`,
    )
    .run(t.tokenHash, t.kind, t.profileId, t.subscriberId, t.photoId, t.expiresAt, t.now);
  return db.prepare('SELECT * FROM access_tokens WHERE id = ?').get(Number(r.lastInsertRowid)) as unknown as TokenRow;
}

export function findTokenByHash(db: Db, tokenHash: string): TokenRow | null {
  return (
    (db.prepare('SELECT * FROM access_tokens WHERE token_hash = ?').get(tokenHash) as TokenRow | undefined) ?? null
  );
}

export function markTokenUsed(db: Db, id: number, now: number): void {
  db.prepare('UPDATE access_tokens SET first_used_at = COALESCE(first_used_at, ?) WHERE id = ?').run(now, id);
}

export function deleteExpiredTokens(db: Db, now: number): number {
  return Number(db.prepare('DELETE FROM access_tokens WHERE expires_at <= ?').run(now).changes);
}
