import type { Context } from 'hono';
import { appUrl } from '../config.js';
import type { Env } from '../env.js';
import { verifyEmailMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { currentSubscriber } from '../lib/session.js';
import { createToken, verifyToken } from '../lib/tokens.js';
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

/** A new blog, unverified until its owner opens the emailed link. */
export function register(env: Env, input: { handle: string; name: string; email: string }): Registration {
  if (findBlogByHandle(env.db, input.handle)) return { ok: false, taken: 'handle' };
  if (findBlogByEmail(env.db, input.email)) return { ok: false, taken: 'email' };
  const blog = createBlog(env.db, { ...input, now: env.now() });
  const { token } = createToken(env, { kind: 'email_verify', blogId: blog.id });
  queueEmail(env, blog.email, verifyEmailMail({ name: blog.name, url: appUrl(env.config, `/verify-email?t=${token}`) }));
  return { ok: true, blog };
}

/** Redeems the emailed link that makes a blog answer at all. Null when it has expired or was never ours. */
export function confirmEmail(env: Env, token: string | undefined): BlogRow | null {
  const verified = verifyToken(env, token, 'email_verify');
  if (!verified.ok) return null;
  markEmailVerified(env.db, verified.row.blog_id, env.now());
  return findBlogById(env.db, verified.row.blog_id);
}
