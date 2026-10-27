import type { Context } from 'hono';
import type { Env } from '../env.js';
import { currentOwner, currentSubscriber, setSubscriberCookie } from '../lib/session.js';
import { verifyToken } from '../lib/tokens.js';
import { findSubscriberById, type SubscriberRow } from '../repo/subscribers.js';
import type { BlogRow } from '../repo/blogs.js';

export type Viewer =
  | { kind: 'owner' }
  | { kind: 'subscriber'; id: number; name: string | null }
  | { kind: 'anonymous' };

export async function viewerOf(c: Context, env: Env, blog: BlogRow): Promise<Viewer> {
  const owner = await currentOwner(c, env);
  if (owner?.id === blog.id) return { kind: 'owner' };
  const sub = await currentSubscriber(c, env, blog.id);
  return sub ? { kind: 'subscriber', id: sub.id, name: sub.name } : { kind: 'anonymous' };
}

export type Redemption =
  | { ok: true; subscriber: SubscriberRow }
  | { ok: false; email: string };

/** An emailed `?t=` becomes a cookie that dies when the link would have; a failure carries the address, to prefill the form. */
export async function redeemSubscriberToken(c: Context, env: Env, blog: BlogRow, token: string): Promise<Redemption> {
  const verified = verifyToken(env, token, 'subscriber');
  const id = verified.ok ? verified.row.subscriber_id : (verified.expired?.subscriber_id ?? null);
  const sub = id !== null ? findSubscriberById(env.db, id) : null;
  const mine = sub && sub.blog_id === blog.id ? sub : null;
  if (!verified.ok || !mine || mine.status !== 'approved') return { ok: false, email: mine?.email ?? '' };
  await setSubscriberCookie(c, env, mine.id, verified.row.expires_at - env.now());
  return { ok: true, subscriber: mine };
}
