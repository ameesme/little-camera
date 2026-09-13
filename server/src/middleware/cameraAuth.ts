// `Authorization: Camera <camera_id>:<secret_hex>` (protocol §4.1).
// The phone sends the camera's own credentials; there is no user session.

import { createHash, timingSafeEqual } from 'node:crypto';
import type { MiddlewareHandler } from 'hono';
import type { Env } from '../env.js';
import { findCamera, touchCamera, type CameraRow } from '../repo/cameras.js';

export type CameraVars = { Variables: { camera: CameraRow } };

export const CAMERA_ID = /^[0-9a-f]{12}$/;
export const SECRET_HEX = /^[0-9a-f]{32}$/;

/** We store sha256 of the 32-char hex string (not of the raw bytes). */
export function hashSecret(secretHex: string): string {
  return createHash('sha256').update(secretHex.toLowerCase(), 'ascii').digest('hex');
}

export function secretMatches(camera: CameraRow, secretHex: string): boolean {
  const a = Buffer.from(hashSecret(secretHex), 'hex');
  const b = Buffer.from(camera.secret_hash, 'hex');
  return a.length === b.length && timingSafeEqual(a, b);
}

export function cameraAuth(env: Env): MiddlewareHandler<CameraVars> {
  return async (c, next) => {
    const header = c.req.header('authorization') ?? '';
    const m = header.match(/^Camera\s+([0-9a-f]{12}):([0-9a-f]{32})$/i);
    if (!m) {
      return c.json({ error: 'unauthorized', message: 'Expected Authorization: Camera <id>:<secret>' }, 401);
    }
    const camera = findCamera(env.db, m[1].toLowerCase());
    if (!camera || !secretMatches(camera, m[2])) {
      return c.json({ error: 'unauthorized', message: 'Unknown camera or wrong secret' }, 401);
    }
    touchCamera(env.db, camera.id, env.now());
    c.set('camera', { ...camera, last_seen: env.now() });
    await next();
  };
}
