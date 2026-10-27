import { Hono } from 'hono';
import { z } from 'zod';
import { blogUrl } from '../config.js';
import * as subscribers from '../core/subscribers.js';
import { boundInfo, claimState, letPhoneIn, relayTelemetry, requestClaim, sayHello } from '../core/cameras.js';
import { register } from '../core/blogs.js';
import { MAX_PHOTO_BYTES, PhotoRejected, feed, uploadPhoto } from '../core/photos.js';
import { VERSION, firmwareState } from '../core/firmware.js';
import type { Env } from '../env.js';
import { CAMERA_ID, SECRET_HEX, cameraAuth, type CameraVars } from '../middleware/cameraAuth.js';
import { countFeedPhotos } from '../repo/photos.js';
import { findBlogById, findBlogByEmail, setAvatarRequested, type BlogRow } from '../repo/blogs.js';
import { HANDLE, RESERVED_HANDLES } from '../lib/handles.js';
import { listSubscribers } from '../repo/subscribers.js';

const helloSchema = z.object({
  camera_id: z.string().regex(CAMERA_ID),
  secret: z.string().regex(SECRET_HEX),
  firmware: z.string().regex(VERSION).optional(),
});

const telemetrySchema = z.object({
  battery_percent: z.number().int().min(0).max(100),
  battery_mv: z.number().int().min(0).max(6000),
  charging: z.boolean(),
  used_bytes: z.number().int().min(0),
  total_bytes: z.number().int().min(0),
  happiness: z.number().int().min(0).max(255),
  mood_elapsed_s: z.number().int().min(0),
});

const blogSchema = z.object({
  name: z.string().trim().min(1).max(60),
  email: z.string().trim().toLowerCase().email().max(200),
  handle: z
    .string()
    .trim()
    .toLowerCase()
    .regex(HANDLE)
    .refine((h) => !RESERVED_HANDLES.has(h)),
});

const FEED_PAGE = 20;

const subscriberSchema = z.object({
  email: z.string().trim().toLowerCase().email().max(200),
  name: z.string().trim().min(1).max(60).nullable().optional(),
});

const error = (code: string, message: string) => ({ error: code, message });
const UNBOUND = error('unbound', 'Link the camera to a blog first');

export function cameraRoutes(env: Env): Hono<CameraVars> {
  const app = new Hono<CameraVars>();

  app.post('/hello', async (c) => {
    const parsed = helloSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { camera_id, secret }'), 400);
    const { camera_id, secret, firmware } = parsed.data;
    const camera = sayHello(env, { id: camera_id, secret, firmware });
    if (!camera) return c.json(error('unauthorized', 'This camera id is registered with a different secret'), 401);
    return c.json({
      camera_id: camera.id,
      short_code: camera.short_code,
      bound: boundInfo(env, camera),
      server_time: env.now(),
    });
  });

  app.use('*', cameraAuth(env));

  const blogOf = (c: { get(k: 'camera'): { blog_id: number | null } }): BlogRow | null => {
    const id = c.get('camera').blog_id;
    return id !== null ? findBlogById(env.db, id) : null;
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
    const blog = blogOf(c);
    return c.json({
      camera_id: camera.id,
      short_code: camera.short_code,
      bound: blog
        ? {
            handle: blog.handle,
            name: blog.name,
            url: blogUrl(env.config, blog.handle),
            photo_count: countFeedPhotos(env.db, blog.id),
          }
        : null,
      avatar: { requested_at: blog?.avatar_requested_at ?? null, has_avatar: blog?.avatar_photo_id != null },
      firmware: firmwareState(env.config, camera.firmware_version),
      // Whether the owner has let this phone in. Until they have, the app shows the blog's name and nothing of its contents.
      claim: claimState(camera),
      subscribers: blog
        ? listSubscribers(env.db, blog.id).map((s) => ({
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

  // What the phone read out of Info (§3.1). Stored as given: the percentage is the camera's own.
  app.post('/telemetry', async (c) => {
    const parsed = telemetrySchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected the Info fields'), 400);
    const t = parsed.data;
    relayTelemetry(env, c.get('camera'), {
      batteryPercent: t.battery_percent,
      batteryMv: t.battery_mv,
      charging: t.charging,
      usedBytes: t.used_bytes,
      totalBytes: t.total_bytes,
      happiness: t.happiness,
      moodElapsedS: t.mood_elapsed_s,
    });
    return c.json({ ok: true });
  });

  // A blog of its own, made by the phone that is holding the camera. Binding is still the QR photo.
  app.post('/blog', async (c) => {
    if (c.get('camera').blog_id !== null) return c.json(error('already_bound', 'This camera already posts to a blog'), 409);
    const parsed = blogSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { name, email, handle }'), 400);
    const made = register(env, parsed.data);
    if (!made.ok) return c.json(error(`${made.taken}_taken`, `That ${made.taken} is already in use`), 409);
    // The phone that made it is let in without a second email; it chose the address.
    letPhoneIn(env, c.get('camera'), made.blog.id);
    return c.json({ handle: made.blog.handle, url: blogUrl(env.config, made.blog.handle) }, 201);
  });

  // Asks the owner, by email, whether this phone may manage the blog. Also the way into a blog that has no camera yet.
  app.post('/claim', async (c) => {
    const camera = c.get('camera');
    const bound = blogOf(c);
    if (bound) {
      requestClaim(env, camera, bound);
      return c.json({ state: 'pending' });
    }
    if (camera.blog_id !== null) return c.json(error('not_found', 'That blog is gone'), 404);
    const parsed = z.object({ email: z.string().trim().toLowerCase().email().max(200) }).safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { email } while the camera is unbound'), 400);
    // Same answer either way, so the form cannot be used to find out who has a camera.
    const blog = findBlogByEmail(env.db, parsed.data.email);
    if (blog) requestClaim(env, camera, blog);
    return c.json({ state: 'pending' });
  });

  app.get('/feed', (c) => {
    const blog = blogOf(c);
    if (!blog) return c.json(UNBOUND, 409);
    const camera = c.get('camera');
    if (claimState(camera) !== 'approved') return c.json(error('unclaimed', 'The owner has not let this phone in'), 403);
    const raw = c.req.query('before');
    const parts = raw?.split('.').map(Number);
    const before = parts?.length === 2 && parts.every(Number.isInteger) ? { uploadedAt: parts[0], id: parts[1] } : undefined;
    const photos = feed(env, blog, FEED_PAGE + 1, before);
    const page = photos.slice(0, FEED_PAGE);
    const last = page[page.length - 1];
    return c.json({
      photos: page,
      next: photos.length > FEED_PAGE && last ? `${last.uploaded_at}.0` : null,
    });
  });

  app.delete('/subscribers/:id', (c) => {
    const blog = blogOf(c);
    if (!blog) return c.json(UNBOUND, 409);
    const subscriber = subscribers.find(env, blog, Number(c.req.param('id')));
    if (!subscriber) return c.json(error('not_found', 'No such subscriber'), 404);
    subscribers.remove(env, subscriber);
    return c.json({ ok: true });
  });

  app.post('/subscribers', async (c) => {
    const blog = blogOf(c);
    if (!blog) return c.json(UNBOUND, 409);
    const parsed = subscriberSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { email, name? }'), 400);
    const { subscriber, created } = subscribers.add(env, blog, parsed.data.email, parsed.data.name ?? null);
    return c.json({ id: subscriber.id }, created ? 201 : 200);
  });

  app.post('/subscribers/:id/:action{approve|block}', (c) => {
    const blog = blogOf(c);
    if (!blog) return c.json(UNBOUND, 409);
    const subscriber = subscribers.find(env, blog, Number(c.req.param('id')));
    if (!subscriber) return c.json(error('not_found', 'No such subscriber'), 404);
    if (c.req.param('action') === 'approve') subscribers.approve(env, subscriber);
    else subscribers.block(env, subscriber);
    return c.json({ ok: true });
  });

  app.post('/request-avatar', (c) => {
    const blog = blogOf(c);
    if (!blog) return c.json(UNBOUND, 409);
    const now = env.now();
    setAvatarRequested(env.db, blog.id, now);
    return c.json({ requested_at: now });
  });

  return app;
}
