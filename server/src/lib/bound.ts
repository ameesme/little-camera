import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import type { CameraRow } from '../repo/cameras.js';
import { findProfileById } from '../repo/profiles.js';

export interface BoundInfo {
  handle: string;
  name: string;
  url: string;
}

/** The `bound` object of the camera API responses, or null for an unbound camera. */
export function boundInfo(env: Env, camera: CameraRow): BoundInfo | null {
  if (camera.profile_id === null) return null;
  const profile = findProfileById(env.db, camera.profile_id);
  if (!profile) return null;
  return { handle: profile.handle, name: profile.name, url: blogUrl(env.config, profile.handle) };
}
