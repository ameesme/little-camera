import type { Context } from 'hono';
import { appUrl } from '../config.js';
import type { Env } from '../env.js';
import { loginLinkMail, verifyEmailMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { currentOwner, currentSubscriber, setOwnerCookie } from '../lib/session.js';
import { createToken, verifyToken } from '../lib/tokens.js';
import type { TokenKind } from '../repo/tokens.js';
import { createBlog, findBlogByEmail, findBlogByHandle, markEmailVerified, type BlogRow } from '../repo/blogs.js';

export type Viewer =
  | { kind: 'owner' }
  | { kind: 'subscriber'; id: number; name: string | null }
  | { kind: 'anonymous' };

/** Who is reading this blog right now, from the two signed cookies. */
export async function viewerOf(c: Context, env: Env, blog: BlogRow): Promise<Viewer> {
  const owner = await currentOwner(c, env);
  if (owner?.id === blog.id) return { kind: 'owner' };
  const sub = await currentSubscriber(c, env, blog.id);
  return sub ? { kind: 'subscriber', id: sub.id, name: sub.name } : { kind: 'anonymous' };
}

export type Registration = { ok: true; blog: BlogRow } | { ok: false; taken: 'handle' | 'email' };

/** A new blog, unverified until its owner opens the emailed link. */
export function register(env: Env, input: { handle: string; name: string; email: string }): Registration {
  if (findBlogByHandle(env.db, input.handle)) return { ok: false, taken: 'handle' };
  if (findBlogByEmail(env.db, input.email)) return { ok: false, taken: 'email' };
  const blog = createBlog(env.db, { ...input, now: env.now() });
  sendOwnerLink(env, blog, 'email_verify');
  return { ok: true, blog };
}

/** Silent on an address nobody registered, so the form cannot be used to find out who has a camera. */
export function sendSignInLink(env: Env, email: string): void {
  const blog = findBlogByEmail(env.db, email);
  // An unverified blog gets a verify link instead: it signs the owner in too, and confirms the address on the way.
  if (blog) sendOwnerLink(env, blog, blog.email_verified_at ? 'owner_login' : 'email_verify');
}

export type OwnerLink = Extract<TokenKind, 'owner_login' | 'email_verify'>;

/** Redeem an emailed owner link into a 30-day cookie. False when it has expired or was never ours. */
export async function signInWithToken(c: Context, env: Env, token: string | undefined, kind: OwnerLink): Promise<boolean> {
  const verified = verifyToken(env, token, kind);
  if (!verified.ok) return false;
  if (kind === 'email_verify') markEmailVerified(env.db, verified.row.blog_id, env.now());
  await setOwnerCookie(c, env, verified.row.blog_id);
  return true;
}

function sendOwnerLink(env: Env, blog: BlogRow, kind: OwnerLink): void {
  const { token } = createToken(env, { kind, blogId: blog.id });
  const path = kind === 'email_verify' ? '/verify-email' : '/login';
  const mail = kind === 'email_verify' ? verifyEmailMail : loginLinkMail;
  queueEmail(env, blog.email, mail({ name: blog.name, url: appUrl(env.config, `${path}?t=${token}`) }));
}
