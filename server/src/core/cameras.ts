import { shortCode } from '@little-camera/pbm';
import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { cameraLinkedMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { hashSecret, secretMatches } from '../middleware/cameraAuth.js';
import { bindCamera, createCamera, findCamera, setCameraFirmware, touchCamera, type CameraRow } from '../repo/cameras.js';
import { findBlogById } from '../repo/blogs.js';
import { deleteCodesForBlog, findLiveCode } from '../repo/codes.js';

export interface BoundInfo {
  handle: string;
  name: string;
  url: string;
}

/** The `bound` object of every camera API response, or null for a camera no blog has claimed. */
export function boundInfo(env: Env, camera: CameraRow): BoundInfo | null {
  const blog = camera.blog_id !== null ? findBlogById(env.db, camera.blog_id) : null;
  if (!blog) return null;
  return { handle: blog.handle, name: blog.name, url: blogUrl(env.config, blog.handle) };
}

export interface HelloInput {
  id: string;
  secret: string;
  firmware?: string;
}

/** Trust on first use: the first secret seen for an id is the one kept, so a later different one is an impostor. */
export function sayHello(env: Env, input: HelloInput): CameraRow | null {
  const now = env.now();
  const known = findCamera(env.db, input.id);
  if (known && !secretMatches(known, input.secret)) return null;

  let camera =
    known ??
    createCamera(env.db, {
      id: input.id,
      secretHash: hashSecret(input.secret),
      shortCode: shortCode(input.id),
      now,
    });
  if (known) touchCamera(env.db, camera.id, now);
  if (input.firmware && input.firmware !== camera.firmware_version) {
    setCameraFirmware(env.db, camera.id, input.firmware, now);
    camera = findCamera(env.db, camera.id)!;
  }
  return camera;
}

export function bind(env: Env, camera: CameraRow, blogId: number): CameraRow {
  bindCamera(env.db, camera.id, blogId, env.now());
  const blog = findBlogById(env.db, blogId);
  if (blog) {
    queueEmail(env, blog.email, cameraLinkedMail({ name: blog.name, blogUrl: blogUrl(env.config, blog.handle) }));
  }
  return findCamera(env.db, camera.id)!;
}

/** A verification code read out of an uploaded photo. Null when no live code matches, or when it is the blog the camera already posts to. */
export function bindByCode(env: Env, camera: CameraRow, code: string): CameraRow | null {
  const live = findLiveCode(env.db, code, env.now());
  if (!live || live.blog_id === camera.blog_id) return null;
  const bound = bind(env, camera, live.blog_id);
  deleteCodesForBlog(env.db, live.blog_id);
  return bound;
}
