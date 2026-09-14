// Subscriber links, shared by the camera API, the owner's web page and
// the "send me a fresh link" form.

import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { freshLinkMail, welcomeMail } from '../emails/index.js';
import { newestFeedPhoto } from '../repo/photos.js';
import { findProfileById, type ProfileRow } from '../repo/profiles.js';
import type { SubscriberRow } from '../repo/subscribers.js';
import { queueEmail } from './email.js';
import { createToken } from './tokens.js';

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

/** Sent when a subscriber is approved or added by the owner. */
export function sendWelcome(env: Env, subscriber: SubscriberRow): void {
  const profile = findProfileById(env.db, subscriber.profile_id);
  if (!profile) return;
  queueEmail(env, subscriber.email, welcomeMail({ ownerName: profile.name, url: subscriberLink(env, profile, subscriber) }));
}

/** Sent from the "this link has expired" page. Caller checks the subscriber is approved. */
export function sendFreshLink(env: Env, profile: ProfileRow, subscriber: SubscriberRow): void {
  queueEmail(env, subscriber.email, freshLinkMail({ ownerName: profile.name, url: subscriberLink(env, profile, subscriber) }));
}
