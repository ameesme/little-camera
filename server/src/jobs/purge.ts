// Hourly: expired tokens and codes, and photos from cameras nobody ever claimed, which are not ours to keep.

import type { Env } from '../env.js';
import { removePhoto } from '../core/photos.js';
import { deleteExpiredCodes } from '../repo/codes.js';
import { orphanPhotosBefore } from '../repo/photos.js';
import { deleteExpiredTokens } from '../repo/tokens.js';

const ORPHAN_DAYS = 7;

export function runPurge(env: Env): { tokens: number; codes: number; photos: number } {
  const now = env.now();
  const tokens = deleteExpiredTokens(env.db, now);
  const codes = deleteExpiredCodes(env.db, now);
  let photos = 0;
  for (const photo of orphanPhotosBefore(env.db, now - ORPHAN_DAYS * 86400)) {
    removePhoto(env, photo);
    photos++;
  }
  return { tokens, codes, photos };
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
