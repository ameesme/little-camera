// Hourly: expired tokens and codes, blogs whose address was never confirmed, and photos from
// cameras nobody ever claimed, which are not ours to keep.

import type { Env } from '../env.js';
import { removePhoto } from '../core/photos.js';
import { deleteExpiredCodes } from '../repo/codes.js';
import { orphanPhotosBefore } from '../repo/photos.js';
import { deleteExpiredTokens } from '../repo/tokens.js';
import { deleteUnverifiedBlogsBefore } from '../repo/blogs.js';

const ORPHAN_DAYS = 7;
// The confirmation link lives an hour (§4.9); a day is long enough that nobody loses a blog they meant to keep.
const UNVERIFIED_HOURS = 24;

export function runPurge(env: Env): { tokens: number; codes: number; blogs: number; photos: number } {
  const now = env.now();
  const tokens = deleteExpiredTokens(env.db, now);
  const codes = deleteExpiredCodes(env.db, now);
  const blogs = deleteUnverifiedBlogsBefore(env.db, now - UNVERIFIED_HOURS * 3600);
  let photos = 0;
  for (const photo of orphanPhotosBefore(env.db, now - ORPHAN_DAYS * 86400)) {
    removePhoto(env, photo);
    photos++;
  }
  return { tokens, codes, blogs, photos };
}

export function startPurgeJob(env: Env, intervalMs = 3600_000): () => void {
  const timer = setInterval(() => {
    try {
      runPurge(env);
    } catch (err) {
      console.error('[purge]', err);
    }
  }, intervalMs);
  timer.unref();
  return () => clearInterval(timer);
}
