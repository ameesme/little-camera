import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { freshLinkMail, newSubscriberMail, welcomeMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { setSubscriberCookie } from '../lib/session.js';
import { createToken, verifyToken } from '../lib/tokens.js';
import { newestFeedPhoto } from '../repo/photos.js';
import { findBlogById, type BlogRow } from '../repo/blogs.js';
import type { Context } from 'hono';
import {
  approveSubscriber,
  blockSubscriber,
  createSubscriber,
  deleteSubscriber,
  findSubscriberByEmail,
  findSubscriberById,
  type SubscriberRow,
} from '../repo/subscribers.js';

export type JoinOutcome = 'sent' | 'pending' | 'already';

/** A 48-hour link to the newest picture, or to the blog root when there is none yet. */
export function subscriberLink(env: Env, blog: BlogRow, subscriber: SubscriberRow): string {
  const newest = newestFeedPhoto(env.db, blog.id);
  const { token } = createToken(env, {
    kind: 'subscriber',
    blogId: blog.id,
    subscriberId: subscriber.id,
    photoId: newest?.id ?? null,
  });
  const base = blogUrl(env.config, blog.handle);
  return newest ? `${base}/p/${newest.public_id}?t=${token}` : `${base}/?t=${token}`;
}

function sendWelcome(env: Env, subscriber: SubscriberRow): void {
  const blog = findBlogById(env.db, subscriber.blog_id);
  if (!blog) return;
  queueEmail(env, subscriber.email, welcomeMail({ ownerName: blog.name, url: subscriberLink(env, blog, subscriber) }));
}

export function sendFreshLink(env: Env, blog: BlogRow, subscriber: SubscriberRow): void {
  queueEmail(env, subscriber.email, freshLinkMail({ ownerName: blog.name, url: subscriberLink(env, blog, subscriber) }));
}

/** Approving twice is a no-op, so a double tap cannot send two welcome links. */
export function approve(env: Env, subscriber: SubscriberRow): SubscriberRow {
  if (subscriber.status === 'approved') return subscriber;
  approveSubscriber(env.db, subscriber.id, env.now());
  const approved = findSubscriberById(env.db, subscriber.id)!;
  sendWelcome(env, approved);
  return approved;
}

export function block(env: Env, subscriber: SubscriberRow): void {
  blockSubscriber(env.db, subscriber.id);
}

/** Gone for good, comments and all. Blocking is the reversible one; this is not. */
export function remove(env: Env, subscriber: SubscriberRow): void {
  deleteSubscriber(env.db, subscriber.id);
}

/** The owner adding someone: approved on the spot, which also approves whoever had already asked to join. */
export function add(env: Env, blog: BlogRow, email: string, name: string | null): { subscriber: SubscriberRow; created: boolean } {
  const existing = findSubscriberByEmail(env.db, blog.id, email);
  if (existing) return { subscriber: approve(env, existing), created: false };
  const subscriber = createSubscriber(env.db, {
    blogId: blog.id,
    email,
    name,
    status: 'approved',
    addedBy: 'owner',
    now: env.now(),
  });
  sendWelcome(env, subscriber);
  return { subscriber, created: true };
}

/** A visitor asking to be let in. Blocked people get the same answer as pending ones. */
export function join(env: Env, blog: BlogRow, email: string): JoinOutcome {
  const existing = findSubscriberByEmail(env.db, blog.id, email);
  if (existing) return existing.status === 'approved' ? 'already' : 'pending';
  createSubscriber(env.db, { blogId: blog.id, email, name: null, status: 'pending', addedBy: 'self', now: env.now() });
  queueEmail(env, blog.email, newSubscriberMail({ ownerName: blog.name, email }));
  return 'sent';
}

/** Null when that id is on another blog's list, so no caller can reach across blogs. */
export function find(env: Env, blog: BlogRow, id: number): SubscriberRow | null {
  const sub = findSubscriberById(env.db, id);
  return sub && sub.blog_id === blog.id ? sub : null;
}

export type Redemption = { ok: true; subscriber: SubscriberRow } | { ok: false; email: string };

/** An emailed `?t=` becomes a cookie that dies when the link would have; a failure carries the address, to prefill the form. */
export async function redeemToken(c: Context, env: Env, blog: BlogRow, token: string): Promise<Redemption> {
  const verified = verifyToken(env, token, 'subscriber');
  const id = verified.ok ? verified.row.subscriber_id : (verified.expired?.subscriber_id ?? null);
  const sub = id !== null ? findSubscriberById(env.db, id) : null;
  const mine = sub && sub.blog_id === blog.id ? sub : null;
  if (!verified.ok || !mine || mine.status !== 'approved') return { ok: false, email: mine?.email ?? '' };
  await setSubscriberCookie(c, env, mine.id, verified.row.expires_at - env.now());
  return { ok: true, subscriber: mine };
}
