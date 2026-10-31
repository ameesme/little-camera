import { Hono } from 'hono';
import { z } from 'zod';
import { randomCode } from '@little-camera/pbm';
import { blogUrl } from '../config.js';
import * as subscribers from '../core/subscribers.js';
import { bind, boundInfo, claimState, letPhoneIn, relayTelemetry, requestAddressConfirmation, requestClaim, sayHello } from '../core/cameras.js';
import { register } from '../core/blogs.js';
import { MAX_COMMENT, MAX_PHOTO_BYTES, PhotoRejected, comment, feed, removePhoto, uploadPhoto } from '../core/photos.js';
import { VERSION, firmwareState } from '../core/firmware.js';
import type { Env } from '../env.js';
import { CAMERA_ID, SECRET_HEX, cameraAuth, type CameraVars } from '../middleware/cameraAuth.js';
import { setCameraVerifiedEmail } from '../repo/cameras.js';
import { countFeedPhotos, findPhotoByPublicId } from '../repo/photos.js';
import { findBlogById, findBlogByEmail, findBlogByHandle, setAvatarRequested, type BlogRow } from '../repo/blogs.js';
import { HANDLE, RESERVED_HANDLES } from '../lib/handles.js';
import { insertCode } from '../repo/codes.js';
import { listSubscribers } from '../repo/subscribers.js';

const helloSchema = z.object({
  camera_id: z.string().regex(CAMERA_ID),
  secret: z.string().regex(SECRET_HEX),
  firmware: z.string().regex(VERSION).optional(),
});

const commentSchema = z.object({
  body: z.string().trim().min(1).max(MAX_COMMENT),
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

  // The blog this camera is being set up for, before the code photo binds it. A phone reopened
  // mid-onboarding reads this rather than starting a second blog for the same owner.
  const pendingBlog = (env: Env, camera: { blog_id: number | null; claim_blog_id: number | null }) => {
    if (camera.blog_id !== null || camera.claim_blog_id === null) return null;
    const blog = findBlogById(env.db, camera.claim_blog_id);
    if (!blog) return null;
    return { handle: blog.handle, name: blog.name, verified: blog.email_verified_at != null };
  };

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
      pending: pendingBlog(env, camera),
      // An address its owner has confirmed but not yet named a blog for (§4.9).
      confirmed_email: camera.blog_id === null ? camera.verified_email : null,
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

  // A blog of its own, named by the phone holding the camera, for the address its owner already
  // confirmed (§4.9). The phone does not send the address: it does not get to choose it here.
  // Binding is still the QR photo.
  app.post('/blog', async (c) => {
    const camera = c.get('camera');
    if (camera.blog_id !== null) return c.json(error('already_bound', 'This camera already posts to a blog'), 409);
    const email = camera.verified_email;
    if (!email) return c.json(error('address_unconfirmed', 'Confirm an email address first'), 409);
    const parsed = blogSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { name, handle }'), 400);
    const made = register(env, { ...parsed.data, email });
    if (!made.ok) return c.json(error(`${made.taken}_taken`, `That ${made.taken} is already in use`), 409);
    // Confirmed once, spent once: the address now belongs to the blog, not to the camera.
    setCameraVerifiedEmail(env.db, camera.id, null);
    // The phone that named it is let in without a second email; its owner confirmed the address.
    letPhoneIn(env, camera, made.blog.id);
    // ponytail: temporary — binds here because the app skips the §5 code photo for now. Remove with it.
    bind(env, camera, made.blog.id);
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
    // Same answer either way, so the form cannot be used to find out who has a camera: an address
    // that owns a blog is mailed a way into it, one nobody knows is mailed a way to start one.
    const blog = findBlogByEmail(env.db, parsed.data.email);
    if (blog) requestClaim(env, camera, blog);
    else requestAddressConfirmation(env, camera, parsed.data.email);
    return c.json({ state: 'pending' });
  });

  // Answered while the owner is still typing, so a taken address is never a refusal after the fact.
  app.get('/handle', (c) => {
    const handle = (c.req.query('handle') ?? '').trim().toLowerCase();
    const allowed = HANDLE.test(handle) && !RESERVED_HANDLES.has(handle);
    const blog = findBlogByHandle(env.db, handle);
    return c.json({ handle, available: allowed && blog === null });
  });

  // The phone shows the code on its own screen and the camera photographs it, so the
  // phone needs the code the web page would otherwise have drawn.
  app.post('/verification-code', (c) => {
    const camera = c.get('camera');
    const blogId = camera.blog_id ?? camera.claim_blog_id;
    if (blogId === null) return c.json(error('no_blog', 'Make a blog or sign in first'), 409);
    if (camera.blog_id !== null) return c.json(error('already_bound', 'This camera already posts to a blog'), 409);
    const code = insertCode(env.db, randomCode(6), blogId, env.now());
    return c.json({ code: code.code, expires_at: code.expires_at }, 201);
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

  app.post('/feed/:id/comments', async (c) => {
    const blog = blogOf(c);
    if (!blog) return c.json(UNBOUND, 409);
    const camera = c.get('camera');
    if (claimState(camera) !== 'approved') return c.json(error('unclaimed', 'The owner has not let this phone in'), 403);
    const photo = findPhotoByPublicId(env.db, c.req.param('id'));
    if (!photo || photo.blog_id !== blog.id) return c.json(error('not_found', 'No such photo'), 404);
    const parsed = commentSchema.safeParse(await c.req.json().catch(() => null));
    if (!parsed.success) return c.json(error('bad_request', 'Expected { body }'), 400);
    // The phone proves the camera, and a claimed camera's blog is the owner's own (§4.9).
    const posted = comment(env, blog, photo, { kind: 'owner' }, { body: parsed.data.body.trim(), name: '' });
    if (!posted.ok) return c.json(error('bad_request', 'Nothing to say'), 400);
    return c.json({ name: posted.name }, 201);
  });

  app.delete('/feed/:id', (c) => {
    const blog = blogOf(c);
    if (!blog) return c.json(UNBOUND, 409);
    const camera = c.get('camera');
    if (claimState(camera) !== 'approved') return c.json(error('unclaimed', 'The owner has not let this phone in'), 403);
    const photo = findPhotoByPublicId(env.db, c.req.param('id'));
    if (!photo || photo.blog_id !== blog.id) return c.json(error('not_found', 'No such photo'), 404);
    removePhoto(env, photo);
    return c.json({ ok: true });
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
