// Only the sha256 is stored, and a token stays valid until it expires because mail scanners prefetch URLs.

import { createHash, randomBytes } from 'node:crypto';
import type { Env } from '../env.js';
import { findTokenByHash, insertToken, markTokenUsed, type TokenKind, type TokenRow } from '../repo/tokens.js';

export const TOKEN_TTL: Record<TokenKind, number> = {
  subscriber: 48 * 3600,
  owner_login: 1 * 3600,
  email_verify: 24 * 3600,
};

export function hashToken(token: string): string {
  return createHash('sha256').update(token).digest('hex');
}

export function createToken(
  env: Env,
  t: { kind: TokenKind; blogId: number; subscriberId?: number | null; photoId?: number | null },
): { token: string; row: TokenRow } {
  const token = randomBytes(32).toString('base64url');
  const now = env.now();
  const row = insertToken(env.db, {
    tokenHash: hashToken(token),
    kind: t.kind,
    blogId: t.blogId,
    subscriberId: t.subscriberId ?? null,
    photoId: t.photoId ?? null,
    expiresAt: now + TOKEN_TTL[t.kind],
    now,
  });
  return { token, row };
}

/** The row when the token is live and of that kind; `expired` lets a caller offer a fresh link instead of an error. */
export function verifyToken(
  env: Env,
  token: string | undefined,
  kind: TokenKind,
): { ok: true; row: TokenRow } | { ok: false; expired: TokenRow | null } {
  if (!token || token.length > 200) return { ok: false, expired: null };
  const row = findTokenByHash(env.db, hashToken(token));
  if (!row || row.kind !== kind) return { ok: false, expired: null };
  if (row.expires_at <= env.now()) return { ok: false, expired: row };
  markTokenUsed(env.db, row.id, env.now());
  return { ok: true, row };
}
