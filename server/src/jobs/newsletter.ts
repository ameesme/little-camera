// The interval is the batching: five photos pushed in one sync become one email, not five.

import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { newsletterMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { createToken } from '../lib/tokens.js';
import { feedPhotosSince } from '../repo/photos.js';
import { findProfileById } from '../repo/profiles.js';
import { listApprovedSubscribers, setLastNotified } from '../repo/subscribers.js';

/** Returns the number of emails queued. */
export function runNewsletter(env: Env): number {
  let sent = 0;
  for (const sub of listApprovedSubscribers(env.db)) {
    const profile = findProfileById(env.db, sub.profile_id);
    if (!profile) continue;
    const since = sub.last_notified_at ?? sub.approved_at ?? sub.created_at;
    const photos = feedPhotosSince(env.db, profile.id, since);
    if (photos.length === 0) continue;
    const newest = photos[photos.length - 1];
    const { token } = createToken(env, {
      kind: 'subscriber',
      profileId: profile.id,
      subscriberId: sub.id,
      photoId: newest.id,
    });
    const url = `${blogUrl(env.config, profile.handle)}/p/${newest.public_id}?t=${token}`;
    queueEmail(env, sub.email, newsletterMail({ ownerName: profile.name, count: photos.length, url }), newest.id);
    // The newest upload covered, not "now", so a photo landing mid-run is caught by the next one.
    setLastNotified(env.db, sub.id, newest.uploaded_at);
    sent++;
  }
  return sent;
}

export function startNewsletterJob(env: Env, intervalMs = 10 * 60_000): () => void {
  const timer = setInterval(() => {
    try {
      runNewsletter(env);
    } catch (err) {
      console.error('[newsletter]', err);
    }
  }, intervalMs);
  timer.unref();
  return () => clearInterval(timer);
}
