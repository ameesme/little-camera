import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { freshLinkMail, newSubscriberMail, welcomeMail } from '../emails/index.js';
import { appUrl } from '../config.js';
import { queueEmail } from '../lib/email.js';
import { createToken } from '../lib/tokens.js';
import { newestFeedPhoto } from '../repo/photos.js';
import { findProfileById, type ProfileRow } from '../repo/profiles.js';
import {
  approveSubscriber,
  blockSubscriber,
  createSubscriber,
  findSubscriberByEmail,
  findSubscriberById,
  type SubscriberRow,
} from '../repo/subscribers.js';

export type JoinOutcome = 'sent' | 'pending' | 'already';

/** A 48-hour link to the newest picture, or to the blog root when there is none yet. */
export function subscriberLink(env: Env, profile: ProfileRow, subscriber: SubscriberRow): string {
  const newest = newestFeedPhoto(env.db, profile.id);
  const { token } = createToken(env, {
    kind: 'subscriber',
    profileId: profile.id,
    subscriberId: subscriber.id,
    photoId: newest?.id ?? null,
  });
  const base = blogUrl(env.config, profile.handle);
  return newest ? `${base}/p/${newest.public_id}?t=${token}` : `${base}/?t=${token}`;
}

function sendWelcome(env: Env, subscriber: SubscriberRow): void {
  const profile = findProfileById(env.db, subscriber.profile_id);
  if (!profile) return;
  queueEmail(env, subscriber.email, welcomeMail({ ownerName: profile.name, url: subscriberLink(env, profile, subscriber) }));
}

export function sendFreshLink(env: Env, profile: ProfileRow, subscriber: SubscriberRow): void {
  queueEmail(env, subscriber.email, freshLinkMail({ ownerName: profile.name, url: subscriberLink(env, profile, subscriber) }));
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

/** The owner adding someone: approved on the spot, which also approves whoever had already asked to join. */
export function add(env: Env, profile: ProfileRow, email: string, name: string | null): { subscriber: SubscriberRow; created: boolean } {
  const existing = findSubscriberByEmail(env.db, profile.id, email);
  if (existing) return { subscriber: approve(env, existing), created: false };
  const subscriber = createSubscriber(env.db, {
    profileId: profile.id,
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
export function join(env: Env, profile: ProfileRow, email: string): JoinOutcome {
  const existing = findSubscriberByEmail(env.db, profile.id, email);
  if (existing) return existing.status === 'approved' ? 'already' : 'pending';
  createSubscriber(env.db, { profileId: profile.id, email, name: null, status: 'pending', addedBy: 'self', now: env.now() });
  queueEmail(env, profile.email, newSubscriberMail({ ownerName: profile.name, email, meUrl: appUrl(env.config, '/me') }));
  return 'sent';
}

/** Null when that id is on someone else's list, so no caller can reach across profiles. */
export function find(env: Env, profile: ProfileRow, id: number): SubscriberRow | null {
  const sub = findSubscriberById(env.db, id);
  return sub && sub.profile_id === profile.id ? sub : null;
}
