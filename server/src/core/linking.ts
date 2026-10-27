import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { bindCamera, findCamera, type CameraRow } from '../repo/cameras.js';
import { deleteCodesForBlog, findLiveCode } from '../repo/codes.js';
import { cameraLinkedMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { findBlogById } from '../repo/blogs.js';

export interface BoundInfo {
  handle: string;
  name: string;
  url: string;
}

/** The `bound` object of every camera API response, or null for an unbound camera. */
export function boundInfo(env: Env, camera: CameraRow): BoundInfo | null {
  const blog = camera.blog_id !== null ? findBlogById(env.db, camera.blog_id) : null;
  if (!blog) return null;
  return { handle: blog.handle, name: blog.name, url: blogUrl(env.config, blog.handle) };
}

export function bind(env: Env, camera: CameraRow, blogId: number): CameraRow {
  bindCamera(env.db, camera.id, blogId, env.now());
  const blog = findBlogById(env.db, blogId);
  if (blog) {
    queueEmail(env, blog.email, cameraLinkedMail({ name: blog.name, blogUrl: blogUrl(env.config, blog.handle) }));
  }
  return findCamera(env.db, camera.id)!;
}

/** A verification code read out of an uploaded photo. Null when no live code matches, or when it is the binding the camera already has. */
export function bindByCode(env: Env, camera: CameraRow, code: string): CameraRow | null {
  const live = findLiveCode(env.db, code, env.now());
  if (!live || live.blog_id === camera.blog_id) return null;
  const bound = bind(env, camera, live.blog_id);
  deleteCodesForBlog(env.db, live.blog_id);
  return bound;
}

