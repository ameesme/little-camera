import type { Context } from 'hono';
import type { Env } from '../env.js';
import { currentSubscriber } from '../lib/session.js';
import { createBlog, findBlogByEmail, findBlogByHandle, findBlogById, markEmailVerified, type BlogRow } from '../repo/blogs.js';

/** `owner` never comes from the web: the owner reads and writes through the app, as the camera (§4.9). */
export type Viewer =
  | { kind: 'owner' }
  | { kind: 'subscriber'; id: number; name: string | null }
  | { kind: 'anonymous' };

/** Who is reading this blog right now, from the subscriber cookie. */
export async function viewerOf(c: Context, env: Env, blog: BlogRow): Promise<Viewer> {
  const sub = await currentSubscriber(c, env, blog.id);
  return sub ? { kind: 'subscriber', id: sub.id, name: sub.name } : { kind: 'anonymous' };
}

export type Registration = { ok: true; blog: BlogRow } | { ok: false; taken: 'handle' | 'email' };

/** A new blog for an address its owner has already confirmed (§4.9), so it answers from birth. */
export function register(env: Env, input: { handle: string; name: string; email: string }): Registration {
  if (findBlogByHandle(env.db, input.handle)) return { ok: false, taken: 'handle' };
  if (findBlogByEmail(env.db, input.email)) return { ok: false, taken: 'email' };
  const blog = createBlog(env.db, { ...input, now: env.now() });
  markEmailVerified(env.db, blog.id, env.now());
  return { ok: true, blog: findBlogById(env.db, blog.id)! };
}
