import { Hono } from 'hono';
import { z } from 'zod';
import { shortCode } from '@little-camera/pbm';
import { blogUrl } from '../config.js';
import * as subscribers from '../core/subscribers.js';
import { boundInfo } from '../core/linking.js';
import { MAX_PHOTO_BYTES, PhotoRejected, uploadPhoto } from '../core/photos.js';
import { VERSION, firmwareState } from '../core/firmware.js';
import type { Env } from '../env.js';
import { batteryPercent } from '../lib/battery.js';
import { CAMERA_ID, SECRET_HEX, cameraAuth, hashSecret, secretMatches, type CameraVars } from '../middleware/cameraAuth.js';
import { createCamera, findCamera, setCameraFirmware, touchCamera } from '../repo/cameras.js';
import { countFeedPhotos } from '../repo/photos.js';
import { findProfileById, setAvatarRequested, type ProfileRow } from '../repo/profiles.js';
import { listSubscribers } from '../repo/subscribers.js';

const helloSchema = z.object({
  camera_id: z.string().regex(CAMERA_ID),
  secret: z.string().regex(SECRET_HEX),
  firmware: z.string().regex(VERSION).optional(),
});

const subscriberSchema = z.object({
  email: z.string().trim().toLowerCase().email().max(200),
  name: z.string().trim().min(1).max(60).nullable().optional(),
});

const error = (code: string, message: string) => ({ error: code, message });
const UNBOUND = error('unbound', 'Link the camera to a blog first');

export function cameraRoutes(env: Env): Hono<CameraVars> {
  const app = new Hono<CameraVars>();

  // Trust on first use: the first secret seen for an id is the one kept, so a later different one is an impostor.
  app.post('/hello', async (c) => {
    const parsed = helloSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { camera_id, secret }'), 400);
    const { camera_id, secret, firmware } = parsed.data;
    const now = env.now();

    let camera = findCamera(env.db, camera_id);
    if (!camera) {
      camera = createCamera(env.db, { id: camera_id, secretHash: hashSecret(secret), shortCode: shortCode(camera_id), now });
    } else if (!secretMatches(camera, secret)) {
      return c.json(error('unauthorized', 'This camera id is registered with a different secret'), 401);
    } else {
      touchCamera(env.db, camera.id, now);
    }
    if (firmware && firmware !== camera.firmware_version) {
      setCameraFirmware(env.db, camera.id, firmware, now);
      camera = findCamera(env.db, camera.id)!;
    }
    return c.json({ camera_id: camera.id, short_code: camera.short_code, bound: boundInfo(env, camera), server_time: now });
  });

  app.use('*', cameraAuth(env));

  const profileOf = (c: { get(k: 'camera'): { profile_id: number | null } }): ProfileRow | null => {
    const id = c.get('camera').profile_id;
    return id !== null ? findProfileById(env.db, id) : null;
  };

  app.post('/photos', async (c) => {
    const type = (c.req.header('content-type') ?? '').split(';')[0].trim().toLowerCase();
    if (type !== 'image/x-portable-bitmap') {
      return c.json(error('unsupported_media_type', 'Content-Type must be image/x-portable-bitmap'), 415);
    }
    if (Number(c.req.header('content-length') ?? 0) > MAX_PHOTO_BYTES) {
      return c.json(error('too_large', 'Photo larger than 16 KB'), 413);
    }
    const index = c.req.header('x-photo-index');
    if (!index || !/^\d{1,5}$/.test(index)) return c.json(error('bad_request', 'X-Photo-Index header required'), 400);
    const capturedAt = c.req.header('x-captured-at');

    try {
      const result = uploadPhoto(env, c.get('camera'), new Uint8Array(await c.req.arrayBuffer()), {
        cameraIndex: Number(index),
        capturedAtHint: capturedAt && /^\d{1,12}$/.test(capturedAt) ? Number(capturedAt) : undefined,
        capturedAtSourceHint: c.req.header('x-captured-at-source')?.toLowerCase(),
      });
      return c.json(
        { id: result.photo.public_id, status: result.status, kind: result.photo.kind, bound: boundInfo(env, result.camera) },
        result.status === 'stored' ? 201 : 200,
      );
    } catch (err) {
      if (err instanceof PhotoRejected) return c.json(error(err.code, err.message), err.status);
      throw err;
    }
  });

  app.get('/status', (c) => {
    const camera = c.get('camera');
    const now = env.now();
    const profile = profileOf(c);
    return c.json({
      camera_id: camera.id,
      short_code: camera.short_code,
      bound: profile
        ? {
            handle: profile.handle,
            name: profile.name,
            url: blogUrl(env.config, profile.handle),
            photo_count: countFeedPhotos(env.db, profile.id),
            battery: batteryPercent(camera.id, camera.bound_at ?? now, now),
          }
        : null,
      avatar: { requested_at: profile?.avatar_requested_at ?? null, has_avatar: profile?.avatar_photo_id != null },
      firmware: firmwareState(env.config, camera.firmware_version),
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

  app.post('/subscribers', async (c) => {
    const profile = profileOf(c);
    if (!profile) return c.json(UNBOUND, 409);
    const parsed = subscriberSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { email, name? }'), 400);
    const { subscriber, created } = subscribers.add(env, profile, parsed.data.email, parsed.data.name ?? null);
    return c.json({ id: subscriber.id }, created ? 201 : 200);
  });

  app.post('/subscribers/:id/:action{approve|block}', (c) => {
    const profile = profileOf(c);
    if (!profile) return c.json(UNBOUND, 409);
    const subscriber = subscribers.find(env, profile, Number(c.req.param('id')));
    if (!subscriber) return c.json(error('not_found', 'No such subscriber'), 404);
    if (c.req.param('action') === 'approve') subscribers.approve(env, subscriber);
    else subscribers.block(env, subscriber);
    return c.json({ ok: true });
  });

  app.post('/request-avatar', (c) => {
    const profile = profileOf(c);
    if (!profile) return c.json(UNBOUND, 409);
    const now = env.now();
    setAvatarRequested(env.db, profile.id, now);
    return c.json({ requested_at: now });
  });

  return app;
}
