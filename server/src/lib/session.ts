// Two cookies, both signed with SESSION_SECRET (hono/cookie HMAC):
//
//   owner  – set after clicking a verify/login link. Domain=.<apex> in
//            production so the owner is also recognised on their own
//            blog subdomain. Skipped on localhost, where browsers ignore
//            Domain anyway.
//   sub    – set on a blog host by a valid subscriber link; host-only so
//            two blogs never share it. Max-Age = what is left of the
//            token, so the cookie dies when the link would have.

import type { Context } from 'hono';
import { deleteCookie, getSignedCookie, setSignedCookie } from 'hono/cookie';
import { apexHost } from '../config.js';
import type { Env } from '../env.js';
import { findProfileById, type ProfileRow } from '../repo/profiles.js';
import { findSubscriberById, type SubscriberRow } from '../repo/subscribers.js';

const OWNER = 'owner';
const SUB = 'sub';
const OWNER_TTL = 30 * 24 * 3600;

function ownerDomain(env: Env): string | undefined {
  const host = apexHost(env.config);
  // "localhost" and bare IPs cannot carry a Domain attribute; anything with
  // a dot is a real domain and its subdomains should see the cookie.
  return host.includes('.') && !/^\d+\.\d+\.\d+\.\d+$/.test(host) ? `.${host}` : undefined;
}

function base(env: Env) {
  return { path: '/', httpOnly: true, sameSite: 'Lax' as const, secure: env.config.publicScheme === 'https' };
}

export async function setOwnerCookie(c: Context, env: Env, profileId: number): Promise<void> {
  await setSignedCookie(c, OWNER, String(profileId), env.config.sessionSecret, {
    ...base(env),
    maxAge: OWNER_TTL,
    domain: ownerDomain(env),
  });
}

export function clearOwnerCookie(c: Context, env: Env): void {
  deleteCookie(c, OWNER, { path: '/', domain: ownerDomain(env) });
}

/** The signed-in owner, or null. Only verified profiles ever get the cookie, but check anyway. */
export async function currentOwner(c: Context, env: Env): Promise<ProfileRow | null> {
  const value = await getSignedCookie(c, env.config.sessionSecret, OWNER);
  if (!value || !/^\d+$/.test(value)) return null;
  const profile = findProfileById(env.db, Number(value));
  return profile?.email_verified_at ? profile : null;
}

export async function setSubscriberCookie(c: Context, env: Env, subscriberId: number, maxAge: number): Promise<void> {
  await setSignedCookie(c, SUB, String(subscriberId), env.config.sessionSecret, { ...base(env), maxAge });
}

/** The approved subscriber of `profileId` identified by the cookie, or null. */
export async function currentSubscriber(c: Context, env: Env, profileId: number): Promise<SubscriberRow | null> {
  const value = await getSignedCookie(c, env.config.sessionSecret, SUB);
  if (!value || !/^\d+$/.test(value)) return null;
  const sub = findSubscriberById(env.db, Number(value));
  return sub && sub.profile_id === profileId && sub.status === 'approved' ? sub : null;
}
