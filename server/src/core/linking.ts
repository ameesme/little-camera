import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { bindCamera, findCamera, type CameraRow } from '../repo/cameras.js';
import { deleteCodesForProfile, findLiveCode } from '../repo/codes.js';
import { cameraLinkedMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { findProfileById } from '../repo/profiles.js';

export interface BoundInfo {
  handle: string;
  name: string;
  url: string;
}

/** The `bound` object of every camera API response, or null for an unbound camera. */
export function boundInfo(env: Env, camera: CameraRow): BoundInfo | null {
  const profile = camera.profile_id !== null ? findProfileById(env.db, camera.profile_id) : null;
  if (!profile) return null;
  return { handle: profile.handle, name: profile.name, url: blogUrl(env.config, profile.handle) };
}

export function bind(env: Env, camera: CameraRow, profileId: number): CameraRow {
  bindCamera(env.db, camera.id, profileId, env.now());
  const profile = findProfileById(env.db, profileId);
  if (profile) {
    queueEmail(env, profile.email, cameraLinkedMail({ name: profile.name, blogUrl: blogUrl(env.config, profile.handle) }));
  }
  return findCamera(env.db, camera.id)!;
}

/** A verification code read out of an uploaded photo. Null when no live code matches, or when it is the binding the camera already has. */
export function bindByCode(env: Env, camera: CameraRow, code: string): CameraRow | null {
  const live = findLiveCode(env.db, code, env.now());
  if (!live || live.profile_id === camera.profile_id) return null;
  const bound = bind(env, camera, live.profile_id);
  deleteCodesForProfile(env.db, live.profile_id);
  return bound;
}

