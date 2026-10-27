import { Hono, type Context } from 'hono';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { z } from 'zod';
import { blankPbm, cropSquare, parsePbm, pbmToPng, setPixel } from '@little-camera/pbm';
import { appUrl, blogUrl } from '../config.js';
import * as subscribers from '../core/subscribers.js';
import { redeemSubscriberToken, viewerOf, type Viewer } from '../core/viewers.js';
import type { Env } from '../env.js';
import { batteryPercent } from '../lib/battery.js';
import { formatMeta } from '../lib/dates.js';
import { findCameraByProfile } from '../repo/cameras.js';
import { commentsForPhotos, insertComment } from '../repo/comments.js';
import {
  countFeedPhotos,
  feedPage,
  findPhotoById,
  findPhotoByPublicId,
  newestFeedPhoto,
  type PhotoRow,
} from '../repo/photos.js';
import type { ProfileRow } from '../repo/profiles.js';
import { findSubscriberByEmail, setSubscriberName } from '../repo/subscribers.js';
import { Simple } from '../views/layout.js';
import { ExpiredLink, Feed, Teaser, blogTitle, type ArticleData } from '../views/blog.js';

type BlogEnv = { Bindings: { profile: ProfileRow } };

const PAGE_SIZE = 10;
const MAX_COMMENT = 1000;
const emailSchema = z.string().trim().toLowerCase().email().max(200);

const JOIN_NOTICES: Record<string, string> = {
  sent: 'Thanks. The owner will let you in.',
  pending: 'Still waiting for the owner to let you in.',
  already: 'You’re already in. Use the link from your email.',
};

export function blogRoutes(env: Env): Hono<BlogEnv> {
  const app = new Hono<BlogEnv>();

  const battery = (profile: ProfileRow): number => {
    const camera = findCameraByProfile(env.db, profile.id);
    const now = env.now();
    return camera ? batteryPercent(camera.id, camera.bound_at ?? now, now) : 100;
  };

  function articles(profile: ProfileRow, photos: PhotoRow[]): ArticleData[] {
    const comments = commentsForPhotos(env.db, photos.map((p) => p.id), profile.name);
    return photos.map((photo) => ({
      photo,
      ...formatMeta(photo.captured_at, env.config.displayTz),
      comments: comments.get(photo.id) ?? [],
    }));
  }

  /** Starting at a photo means "everything from there on", so an emailed link opens on its picture without a scroll script. */
  function feed(profile: ProfileRow, viewer: Viewer, opts: { before?: { uploadedAt: number; id: number }; notice?: string; startAt?: PhotoRow }) {
    const before = opts.startAt ? { uploadedAt: opts.startAt.uploaded_at, id: opts.startAt.id + 1 } : opts.before;
    const photos = feedPage(env.db, profile.id, PAGE_SIZE + 1, before);
    const page = photos.slice(0, PAGE_SIZE);
    const last = page[page.length - 1];
    return (
      <Feed
        profile={profile}
        viewer={viewer}
        battery={battery(profile)}
        meUrl={appUrl(env.config, '/me')}
        avatarUrl={`${blogUrl(env.config, profile.handle)}/avatar.png`}
        blogRoot={`${blogUrl(env.config, profile.handle)}/`}
        articles={articles(profile, page)}
        moreHref={photos.length > PAGE_SIZE && last ? `/?before=${last.uploaded_at}&bid=${last.id}` : null}
        notice={opts.notice}
      />
    );
  }

  function teaser(profile: ProfileRow, notice?: string) {
    const newest = newestFeedPhoto(env.db, profile.id);
    return (
      <Teaser
        profile={profile}
        battery={battery(profile)}
        meUrl={appUrl(env.config, '/me')}
        avatarUrl={`${blogUrl(env.config, profile.handle)}/avatar.png`}
        blogRoot={`${blogUrl(env.config, profile.handle)}/`}
        articles={newest ? articles(profile, [newest]) : []}
        hiddenCount={Math.max(0, countFeedPhotos(env.db, profile.id) - 1)}
        notice={notice}
      />
    );
  }

  /** Every emailed link lands on a page that redeems it first; `after` is where the browser goes once it has. */
  async function withToken(c: Context<BlogEnv>, profile: ProfileRow, token: string, after: string) {
    const redeemed = await redeemSubscriberToken(c, env, profile, token);
    if (redeemed.ok) return c.redirect(after, 303);
    return c.html(<ExpiredLink email={redeemed.email} />, 410);
  }

  app.get('/', async (c) => {
    const profile = c.env.profile;
    const token = c.req.query('t');
    if (token) return withToken(c, profile, token, '/');

    const viewer = await viewerOf(c, env, profile);
    const notice = JOIN_NOTICES[c.req.query('joined') ?? ''];
    if (viewer.kind === 'anonymous') return c.html(teaser(profile, notice));
    const beforeAt = Number(c.req.query('before'));
    const beforeId = Number(c.req.query('bid'));
    const before = beforeAt > 0 && beforeId > 0 ? { uploadedAt: beforeAt, id: beforeId } : undefined;
    return c.html(feed(profile, viewer, { before, notice }));
  });

  app.get('/p/:public_id', async (c) => {
    const profile = c.env.profile;
    const photo = visiblePhoto(env, profile, c.req.param('public_id'));
    if (!photo) return c.notFound();
    const token = c.req.query('t');
    if (token) return withToken(c, profile, token, `/p/${photo.public_id}`);

    const viewer = await viewerOf(c, env, profile);
    if (viewer.kind === 'anonymous') return c.redirect('/', 303);
    return c.html(feed(profile, viewer, { startAt: photo }));
  });

  // Always the same answer, so the form cannot be used to probe who subscribes.
  app.post('/request-link', async (c) => {
    const profile = c.env.profile;
    const form = await c.req.parseBody();
    const email = emailSchema.safeParse(form.email);
    const sub = email.success ? findSubscriberByEmail(env.db, profile.id, email.data) : null;
    if (sub?.status === 'approved') subscribers.sendFreshLink(env, profile, sub);
    return c.html(
      <Simple title="Check your email" heading="Check your email">
        <p>If that address is on the list, a fresh link is on its way.</p>
      </Simple>,
    );
  });

  app.post('/subscribe', async (c) => {
    const profile = c.env.profile;
    const json = wantsJson(c);
    const raw = (c.req.header('content-type') ?? '').includes('application/json')
      ? await c.req.json().catch(() => ({}))
      : await c.req.parseBody();
    const email = emailSchema.safeParse((raw as { email?: unknown }).email);
    if (!email.success) return json ? c.json({ error: 'bad_email' }, 400) : c.redirect('/#join', 303);
    const state = subscribers.join(env, profile, email.data);
    return json ? c.json({ state }) : c.redirect(`/?joined=${state}`, 303);
  });

  app.post('/p/:public_id/comments', async (c) => {
    const profile = c.env.profile;
    const photo = visiblePhoto(env, profile, c.req.param('public_id'));
    if (!photo) return c.notFound();
    const json = wantsJson(c);
    const viewer = await viewerOf(c, env, profile);
    if (viewer.kind === 'anonymous') return json ? c.json({ error: 'unauthorized' }, 401) : c.redirect('/', 303);

    const form = await c.req.parseBody();
    const body = String(form.body ?? '').trim();
    const back = (error: string) => (json ? c.json({ error }, 400) : c.redirect(`/p/${photo.public_id}`, 303));
    if (!body || body.length > MAX_COMMENT) return back('bad_body');

    let name = profile.name;
    let subscriberId: number | null = null;
    if (viewer.kind === 'subscriber') {
      subscriberId = viewer.id;
      // A subscriber names themselves on their first comment and never again.
      if (viewer.name) {
        name = viewer.name;
      } else {
        const given = String(form.name ?? '').trim().slice(0, 60);
        if (!given) return back('name_required');
        setSubscriberName(env.db, viewer.id, given);
        name = given;
      }
    }
    insertComment(env.db, { photoId: photo.id, subscriberId, body, now: env.now() });
    return json ? c.json({ name, body }) : c.redirect(`/p/${photo.public_id}`, 303);
  });

  // The stored PBM as a 1-bit PNG. Addressed by an unguessable id and never changed, so it can be cached forever.
  app.get('/photos/:file', (c) => {
    const profile = c.env.profile;
    const named = c.req.param('file').match(/^([a-z0-9]{12})\.png$/);
    const photo = named ? visiblePhoto(env, profile, named[1]) : null;
    if (!photo) return c.notFound();
    const etag = `"${photo.sha256}"`;
    if (c.req.header('if-none-match') === etag) return c.body(null, 304);
    return c.body(pbmToPng(readPbm(env, photo.path)), 200, {
      'content-type': 'image/png',
      'cache-control': 'public, max-age=31536000, immutable',
      etag,
    });
  });

  app.get('/avatar.png', (c) => {
    const profile = c.env.profile;
    const avatar = profile.avatar_photo_id !== null ? findPhotoById(env.db, profile.avatar_photo_id) : null;
    const png = avatar ? pbmToPng(cropSquare(readPbm(env, avatar.path), 240)) : pbmToPng(checker(240));
    const etag = avatar ? `"${avatar.sha256}"` : '"checker"';
    if (c.req.header('if-none-match') === etag) return c.body(null, 304);
    return c.body(png, 200, { 'content-type': 'image/png', 'cache-control': 'public, max-age=3600', etag });
  });

  return app;
}

const wantsJson = (c: Context<BlogEnv>): boolean => (c.req.header('accept') ?? '').includes('application/json');

/** A photo of this blog that the public is allowed to see; verification shots never are. */
function visiblePhoto(env: Env, profile: ProfileRow, publicId: string): PhotoRow | null {
  const photo = findPhotoByPublicId(env.db, publicId);
  if (!photo || photo.profile_id !== profile.id || photo.kind === 'verification') return null;
  return photo;
}

function readPbm(env: Env, path: string) {
  return parsePbm(new Uint8Array(readFileSync(join(env.config.dataDir, path))));
}

/** The 2 px checker, which stands in for a profile picture until there is one. */
function checker(size: number) {
  const pbm = blankPbm(size, size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) if (((x >> 1) + (y >> 1)) % 2 === 0) setPixel(pbm, x, y, true);
  }
  return pbm;
}
