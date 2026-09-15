// The camera API (protocol §4). Mounted at /api/camera on the apex host.
// The bridge app calls these with the camera's own credentials.

import { Hono } from 'hono';
import { z } from 'zod';
import { shortCode } from '@little-camera/pbm';
import type { Env } from '../env.js';
import { batteryPercent } from '../lib/battery.js';
import { boundInfo } from '../lib/bound.js';
import { IngestError, MAX_PHOTO_BYTES, ingestPhoto } from '../lib/ingest.js';
import { sendWelcome } from '../lib/welcome.js';
import { CAMERA_ID, SECRET_HEX, cameraAuth, hashSecret, secretMatches, type CameraVars } from '../middleware/cameraAuth.js';
import { createCamera, findCamera, touchCamera } from '../repo/cameras.js';
import { countFeedPhotos } from '../repo/photos.js';
import { findProfileById, setAvatarRequested } from '../repo/profiles.js';
import {
  approveSubscriber,
  blockSubscriber,
  createSubscriber,
  findSubscriberByEmail,
  findSubscriberById,
  listSubscribers,
} from '../repo/subscribers.js';
import { blogUrl } from '../config.js';

const helloSchema = z.object({
  camera_id: z.string().regex(CAMERA_ID),
  secret: z.string().regex(SECRET_HEX),
});

const subscriberSchema = z.object({
  email: z.string().trim().toLowerCase().email().max(200),
  name: z.string().trim().min(1).max(60).nullable().optional(),
});

const error = (code: string, message: string) => ({ error: code, message });

export function cameraRoutes(env: Env): Hono<CameraVars> {
  const app = new Hono<CameraVars>();

  // 4.2 hello: trust on first use. The first secret we see for an id is
  // the one we keep; a different one later is an impostor (or a camera
  // that lost its NVS, which needs a human anyway).
  app.post('/hello', async (c) => {
    const parsed = helloSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { camera_id, secret }'), 400);
    const { camera_id, secret } = parsed.data;
    const now = env.now();
    let camera = findCamera(env.db, camera_id);
    if (!camera) {
      camera = createCamera(env.db, { id: camera_id, secretHash: hashSecret(secret), shortCode: shortCode(camera_id), now });
    } else if (!secretMatches(camera, secret)) {
      return c.json(error('unauthorized', 'This camera id is registered with a different secret'), 401);
    } else {
      touchCamera(env.db, camera.id, now);
    }
    return c.json({ camera_id: camera.id, short_code: camera.short_code, bound: boundInfo(env, camera), server_time: now });
  });

  app.use('*', cameraAuth(env));

  // 4.3 photos
  app.post('/photos', async (c) => {
    const type = (c.req.header('content-type') ?? '').split(';')[0].trim().toLowerCase();
    if (type !== 'image/x-portable-bitmap') {
      return c.json(error('unsupported_media_type', 'Content-Type must be image/x-portable-bitmap'), 415);
    }
    const declared = Number(c.req.header('content-length') ?? 0);
    if (declared > MAX_PHOTO_BYTES) return c.json(error('too_large', 'Photo larger than 16 KB'), 413);
    const indexHeader = c.req.header('x-photo-index');
    if (!indexHeader || !/^\d{1,5}$/.test(indexHeader)) return c.json(error('bad_request', 'X-Photo-Index header required'), 400);
    const capturedAt = c.req.header('x-captured-at');
    const body = new Uint8Array(await c.req.arrayBuffer());
    try {
      const result = ingestPhoto(env, c.get('camera'), body, {
        cameraIndex: Number(indexHeader),
        capturedAtHint: capturedAt && /^\d{1,12}$/.test(capturedAt) ? Number(capturedAt) : undefined,
        capturedAtSourceHint: c.req.header('x-captured-at-source')?.toLowerCase(),
      });
      return c.json(
        { id: result.photo.public_id, status: result.status, kind: result.photo.kind, bound: boundInfo(env, result.camera) },
        result.status === 'stored' ? 201 : 200,
      );
    } catch (err) {
      if (err instanceof IngestError) return c.json(error(err.code, err.message), err.status);
      throw err;
    }
  });

  // 4.4 status
  app.get('/status', (c) => {
    const camera = c.get('camera');
    const now = env.now();
    const profile = camera.profile_id !== null ? findProfileById(env.db, camera.profile_id) : null;
    const bound = profile
      ? {
          handle: profile.handle,
          name: profile.name,
          url: blogUrl(env.config, profile.handle),
          photo_count: countFeedPhotos(env.db, profile.id),
          battery: batteryPercent(camera.id, camera.bound_at ?? now, now),
        }
      : null;
    return c.json({
      camera_id: camera.id,
      short_code: camera.short_code,
      bound,
      avatar: { requested_at: profile?.avatar_requested_at ?? null, has_avatar: profile?.avatar_photo_id != null },
      subscribers: profile
        ? listSubscribers(env.db, profile.id).map((s) => ({
            id: s.id,
            email: s.email,
            name: s.name,
            status: s.status,
            added_by: s.added_by,
          }))
        : [],
      server_time: now,
    });
  });

  // 4.5 subscribers: bound cameras only
  const requireBound = (c: { get(k: 'camera'): { profile_id: number | null } }) => {
    const camera = c.get('camera');
    return camera.profile_id !== null ? findProfileById(env.db, camera.profile_id) : null;
  };

  app.post('/subscribers', async (c) => {
    const profile = requireBound(c);
    if (!profile) return c.json(error('unbound', 'Link the camera to a blog first'), 409);
    const parsed = subscriberSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { email, name? }'), 400);
    const { email, name } = parsed.data;
    const now = env.now();
    const existing = findSubscriberByEmail(env.db, profile.id, email);
    if (existing) {
      // Adding someone who already asked to join is the same as approving them.
      if (existing.status !== 'approved') {
        approveSubscriber(env.db, existing.id, now);
        sendWelcome(env, findSubscriberById(env.db, existing.id)!);
      }
      return c.json({ id: existing.id }, 200);
    }
    const sub = createSubscriber(env.db, { profileId: profile.id, email, name: name ?? null, status: 'approved', addedBy: 'owner', now });
    sendWelcome(env, sub);
    return c.json({ id: sub.id }, 201);
  });

  const subscriberOf = (c: { get(k: 'camera'): { profile_id: number | null }; req: { param(k: 'id'): string } }) => {
    const profile = requireBound(c);
    if (!profile) return { profile: null, sub: null };
    const sub = findSubscriberById(env.db, Number(c.req.param('id')));
    return { profile, sub: sub && sub.profile_id === profile.id ? sub : null };
  };

  app.post('/subscribers/:id/approve', (c) => {
    const { profile, sub } = subscriberOf(c);
    if (!profile) return c.json(error('unbound', 'Link the camera to a blog first'), 409);
    if (!sub) return c.json(error('not_found', 'No such subscriber'), 404);
    if (sub.status !== 'approved') {
      approveSubscriber(env.db, sub.id, env.now());
      sendWelcome(env, findSubscriberById(env.db, sub.id)!);
    }
    return c.json({ ok: true });
  });

  app.post('/subscribers/:id/block', (c) => {
    const { profile, sub } = subscriberOf(c);
    if (!profile) return c.json(error('unbound', 'Link the camera to a blog first'), 409);
    if (!sub) return c.json(error('not_found', 'No such subscriber'), 404);
    blockSubscriber(env.db, sub.id);
    return c.json({ ok: true });
  });

  // 4.6 request-avatar
  app.post('/request-avatar', (c) => {
    const profile = requireBound(c);
    if (!profile) return c.json(error('unbound', 'Link the camera to a blog first'), 409);
    const now = env.now();
    setAvatarRequested(env.db, profile.id, now);
    return c.json({ requested_at: now });
  });

  return app;
}
