// Shared test scaffolding: an in-memory database, a temp DATA_DIR, a clock
// the test can move, and small helpers that talk to the app the way the
// bridge app and a browser would.

import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodePbm, parsePbm, type PbmMeta } from '@little-camera/pbm';
import { createApp } from '../src/app.js';
import { APP_PREFIX, loadConfig } from '../src/config.js';
import { openDb } from '../src/db.js';
import type { Env } from '../src/env.js';
import { createProfile, markEmailVerified, type ProfileRow } from '../src/repo/profiles.js';

export const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'packages', 'pbm', 'fixtures');
export const APEX = 'localhost:3000';

export function fixture(name: string, meta?: PbmMeta): Uint8Array {
  const bytes = new Uint8Array(readFileSync(join(FIXTURES, `${name}.pbm`)));
  if (!meta) return bytes;
  const pbm = parsePbm(bytes);
  return encodePbm(pbm.width, pbm.height, pbm.rows, meta);
}

export interface TestWorld {
  env: Env;
  app: ReturnType<typeof createApp>;
  clock: { t: number };
  cleanup: () => void;
}

export function makeWorld(): TestWorld {
  const dataDir = mkdtempSync(join(tmpdir(), 'lc-test-'));
  const clock = { t: 1_757_789_000 }; // Sat 13 Sep 2025 18:43:20 UTC
  const config = loadConfig({
    BASE_DOMAIN: APEX,
    DATA_DIR: dataDir,
    SESSION_SECRET: 'test-secret',
    NODE_ENV: 'test',
    PUBLIC_SCHEME: 'http',
    DISPLAY_TZ: 'Europe/Amsterdam',
  });
  const env: Env = { db: openDb(':memory:'), config, now: () => clock.t };
  const app = createApp(env);
  return { env, app, clock, cleanup: () => rmSync(dataDir, { recursive: true, force: true }) };
}

export function makeProfile(env: Env, p: { handle: string; name: string; email: string; verified?: boolean }): ProfileRow {
  const profile = createProfile(env.db, { handle: p.handle, name: p.name, email: p.email, now: env.now() });
  if (p.verified !== false) markEmailVerified(env.db, profile.id, env.now());
  return { ...profile, email_verified_at: p.verified !== false ? env.now() : null };
}

export const CAM = { id: '7cdfa1e2b3c4', secret: '9f2c0000111122223333444455556666' };

export function apex(path: string, init: RequestInit = {}): Request {
  const headers = new Headers(init.headers);
  headers.set('host', APEX);
  return new Request(`http://${APEX}${path}`, { ...init, headers });
}

/**
 * A page of the app. They live under APP_PREFIX now that the apex root belongs
 * to the landing page, and every test that opens one goes through here so the
 * prefix is written down once.
 */
export function appPage(path: string, init: RequestInit = {}): Request {
  return apex(`${APP_PREFIX}${path}`, init);
}

export function blog(handle: string, path: string, init: RequestInit = {}): Request {
  const headers = new Headers(init.headers);
  headers.set('host', `${handle}.${APEX}`);
  return new Request(`http://${handle}.${APEX}${path}`, { ...init, headers });
}

export async function hello(world: TestWorld, cam = CAM) {
  const res = await world.app.request(
    apex('/api/camera/hello', {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ camera_id: cam.id, secret: cam.secret }),
    }),
  );
  return { status: res.status, body: await res.json() };
}

/** Copy into a fresh ArrayBuffer-backed view: fetch's BodyInit type refuses Buffer-backed views. */
export function body(bytes: Uint8Array): Uint8Array<ArrayBuffer> {
  return new Uint8Array(bytes);
}

export function cameraHeaders(cam = CAM, extra: Record<string, string> = {}): Record<string, string> {
  return { authorization: `Camera ${cam.id}:${cam.secret}`, ...extra };
}

export async function upload(
  world: TestWorld,
  bytes: Uint8Array,
  index: number,
  extra: Record<string, string> = {},
  cam = CAM,
) {
  const res = await world.app.request(
    apex('/api/camera/photos', {
      method: 'POST',
      headers: cameraHeaders(cam, {
        'content-type': 'image/x-portable-bitmap',
        'x-photo-index': String(index),
        'content-length': String(bytes.length),
        ...extra,
      }),
      body: body(bytes),
    }),
  );
  return { status: res.status, body: await res.json() };
}

export async function cameraGet(world: TestWorld, path: string, cam = CAM) {
  const res = await world.app.request(apex(`/api/camera${path}`, { headers: cameraHeaders(cam) }));
  return { status: res.status, body: await res.json() };
}

export async function cameraPost(world: TestWorld, path: string, json: unknown, cam = CAM) {
  const res = await world.app.request(
    apex(`/api/camera${path}`, {
      method: 'POST',
      headers: cameraHeaders(cam, { 'content-type': 'application/json' }),
      body: JSON.stringify(json),
    }),
  );
  return { status: res.status, body: await res.json() };
}

/** Parse a `Set-Cookie` header list into a Cookie request header. */
export function cookieJar(res: Response): string {
  return res.headers
    .getSetCookie()
    .map((c) => c.split(';')[0])
    .join('; ');
}
