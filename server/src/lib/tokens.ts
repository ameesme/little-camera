// Links in emails carry a token. We store only its sha256, so a database
// leak does not hand out working links, and we keep a token valid until it
// expires rather than burning it on first use: mail clients and link
// scanners prefetch URLs, and a link that dies before the human clicks it
// is the number one support question for magic links.

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
  t: { kind: TokenKind; profileId: number; subscriberId?: number | null; photoId?: number | null },
): { token: string; row: TokenRow } {
  const token = randomBytes(32).toString('base64url');
  const now = env.now();
  const row = insertToken(env.db, {
    tokenHash: hashToken(token),
    kind: t.kind,
    profileId: t.profileId,
    subscriberId: t.subscriberId ?? null,
    photoId: t.photoId ?? null,
    expiresAt: now + TOKEN_TTL[t.kind],
    now,
  });
  return { token, row };
}

/**
 * Look a token up. Returns the row when it exists, is of the expected kind
 * and has not expired; records the first use. `expired` lets the caller
 * show "this link has expired" (with the email prefilled) instead of a
 * generic error.
 */
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
