// One cookie, signed with SESSION_SECRET. There is no owner session: the owner uses the app.

import type { Context } from 'hono';
import { getSignedCookie, setSignedCookie } from 'hono/cookie';
import type { Env } from '../env.js';
import { findSubscriberById, type SubscriberRow } from '../repo/subscribers.js';

const SUB = 'sub';

function base(env: Env) {
  return { path: '/', httpOnly: true, sameSite: 'Lax' as const, secure: env.config.publicScheme === 'https' };
}

export async function setSubscriberCookie(c: Context, env: Env, subscriberId: number, maxAge: number): Promise<void> {
  await setSignedCookie(c, SUB, String(subscriberId), env.config.sessionSecret, { ...base(env), maxAge });
}

/** The approved subscriber of `blogId` identified by the cookie, or null. */
export async function currentSubscriber(c: Context, env: Env, blogId: number): Promise<SubscriberRow | null> {
  const value = await getSignedCookie(c, env.config.sessionSecret, SUB);
  if (!value || !/^\d+$/.test(value)) return null;
  const sub = findSubscriberById(env.db, Number(value));
  return sub && sub.blog_id === blogId && sub.status === 'approved' ? sub : null;
}
