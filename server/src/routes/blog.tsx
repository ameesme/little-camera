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
import { findCameraByBlog } from '../repo/cameras.js';
import { commentsForPhotos, insertComment } from '../repo/comments.js';
import {
  countFeedPhotos,
  feedPage,
  findPhotoById,
  findPhotoByPublicId,
  newestFeedPhoto,
  type PhotoRow,
} from '../repo/photos.js';
import type { BlogRow } from '../repo/blogs.js';
import { findSubscriberByEmail, setSubscriberName } from '../repo/subscribers.js';
import { Simple } from '../views/layout.js';
import { ExpiredLink, Feed, Teaser, blogTitle, type ArticleData } from '../views/blog.js';

type BlogEnv = { Bindings: { blog: BlogRow } };

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

  const battery = (blog: BlogRow): number => {
    const camera = findCameraByBlog(env.db, blog.id);
    const now = env.now();
    return camera ? batteryPercent(camera.id, camera.bound_at ?? now, now) : 100;
  };

  function articles(blog: BlogRow, photos: PhotoRow[]): ArticleData[] {
    const comments = commentsForPhotos(env.db, photos.map((p) => p.id), blog.name);
    return photos.map((photo) => ({
      photo,
      ...formatMeta(photo.captured_at, env.config.displayTz),
      comments: comments.get(photo.id) ?? [],
    }));
  }

  /** Starting at a photo means "everything from there on", so an emailed link opens on its picture without a scroll script. */
  function feed(blog: BlogRow, viewer: Viewer, opts: { before?: { uploadedAt: number; id: number }; notice?: string; startAt?: PhotoRow }) {
    const before = opts.startAt ? { uploadedAt: opts.startAt.uploaded_at, id: opts.startAt.id + 1 } : opts.before;
    const photos = feedPage(env.db, blog.id, PAGE_SIZE + 1, before);
    const page = photos.slice(0, PAGE_SIZE);
    const last = page[page.length - 1];
    return (
      <Feed
        blog={blog}
        viewer={viewer}
        battery={battery(blog)}
        meUrl={appUrl(env.config, '/me')}
        avatarUrl={`${blogUrl(env.config, blog.handle)}/avatar.png`}
        blogRoot={`${blogUrl(env.config, blog.handle)}/`}
        articles={articles(blog, page)}
        moreHref={photos.length > PAGE_SIZE && last ? `/?before=${last.uploaded_at}&bid=${last.id}` : null}
        notice={opts.notice}
      />
    );
  }

  function teaser(blog: BlogRow, notice?: string) {
    const newest = newestFeedPhoto(env.db, blog.id);
    return (
      <Teaser
        blog={blog}
        battery={battery(blog)}
        meUrl={appUrl(env.config, '/me')}
        avatarUrl={`${blogUrl(env.config, blog.handle)}/avatar.png`}
        blogRoot={`${blogUrl(env.config, blog.handle)}/`}
        articles={newest ? articles(blog, [newest]) : []}
        hiddenCount={Math.max(0, countFeedPhotos(env.db, blog.id) - 1)}
        notice={notice}
      />
    );
  }

  /** Every emailed link lands on a page that redeems it first; `after` is where the browser goes once it has. */
  async function withToken(c: Context<BlogEnv>, blog: BlogRow, token: string, after: string) {
    const redeemed = await redeemSubscriberToken(c, env, blog, token);
    if (redeemed.ok) return c.redirect(after, 303);
    return c.html(<ExpiredLink email={redeemed.email} />, 410);
  }

  app.get('/', async (c) => {
    const blog = c.env.blog;
    const token = c.req.query('t');
    if (token) return withToken(c, blog, token, '/');

    const viewer = await viewerOf(c, env, blog);
    const notice = JOIN_NOTICES[c.req.query('joined') ?? ''];
    if (viewer.kind === 'anonymous') return c.html(teaser(blog, notice));
    const beforeAt = Number(c.req.query('before'));
    const beforeId = Number(c.req.query('bid'));
    const before = beforeAt > 0 && beforeId > 0 ? { uploadedAt: beforeAt, id: beforeId } : undefined;
    return c.html(feed(blog, viewer, { before, notice }));
  });

  app.get('/p/:public_id', async (c) => {
    const blog = c.env.blog;
    const photo = visiblePhoto(env, blog, c.req.param('public_id'));
    if (!photo) return c.notFound();
    const token = c.req.query('t');
    if (token) return withToken(c, blog, token, `/p/${photo.public_id}`);

    const viewer = await viewerOf(c, env, blog);
    if (viewer.kind === 'anonymous') return c.redirect('/', 303);
    return c.html(feed(blog, viewer, { startAt: photo }));
  });

  // Always the same answer, so the form cannot be used to probe who subscribes.
  app.post('/request-link', async (c) => {
    const blog = c.env.blog;
    const form = await c.req.parseBody();
    const email = emailSchema.safeParse(form.email);
    const sub = email.success ? findSubscriberByEmail(env.db, blog.id, email.data) : null;
    if (sub?.status === 'approved') subscribers.sendFreshLink(env, blog, sub);
    return c.html(
      <Simple title="Check your email" heading="Check your email">
        <p>If that address is on the list, a fresh link is on its way.</p>
      </Simple>,
    );
  });

  app.post('/subscribe', async (c) => {
    const blog = c.env.blog;
    const json = wantsJson(c);
    const raw = (c.req.header('content-type') ?? '').includes('application/json')
      ? await c.req.json().catch(() => ({}))
      : await c.req.parseBody();
    const email = emailSchema.safeParse((raw as { email?: unknown }).email);
    if (!email.success) return json ? c.json({ error: 'bad_email' }, 400) : c.redirect('/#join', 303);
    const state = subscribers.join(env, blog, email.data);
    return json ? c.json({ state }) : c.redirect(`/?joined=${state}`, 303);
  });

  app.post('/p/:public_id/comments', async (c) => {
    const blog = c.env.blog;
    const photo = visiblePhoto(env, blog, c.req.param('public_id'));
    if (!photo) return c.notFound();
    const json = wantsJson(c);
    const viewer = await viewerOf(c, env, blog);
    if (viewer.kind === 'anonymous') return json ? c.json({ error: 'unauthorized' }, 401) : c.redirect('/', 303);

    const form = await c.req.parseBody();
    const body = String(form.body ?? '').trim();
    const back = (error: string) => (json ? c.json({ error }, 400) : c.redirect(`/p/${photo.public_id}`, 303));
    if (!body || body.length > MAX_COMMENT) return back('bad_body');

    let name = blog.name;
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
    const blog = c.env.blog;
    const named = c.req.param('file').match(/^([a-z0-9]{12})\.png$/);
    const photo = named ? visiblePhoto(env, blog, named[1]) : null;
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
    const blog = c.env.blog;
    const avatar = blog.avatar_photo_id !== null ? findPhotoById(env.db, blog.avatar_photo_id) : null;
    const png = avatar ? pbmToPng(cropSquare(readPbm(env, avatar.path), 240)) : pbmToPng(checker(240));
    const etag = avatar ? `"${avatar.sha256}"` : '"checker"';
    if (c.req.header('if-none-match') === etag) return c.body(null, 304);
    return c.body(png, 200, { 'content-type': 'image/png', 'cache-control': 'public, max-age=3600', etag });
  });

  return app;
}

const wantsJson = (c: Context<BlogEnv>): boolean => (c.req.header('accept') ?? '').includes('application/json');

/** A photo of this blog that the public is allowed to see; verification shots never are. */
function visiblePhoto(env: Env, blog: BlogRow, publicId: string): PhotoRow | null {
  const photo = findPhotoByPublicId(env.db, publicId);
  if (!photo || photo.blog_id !== blog.id || photo.kind === 'verification') return null;
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
