// One owner's blog, served on <handle>.<apex>. The resolved profile comes
// in as c.env.profile from the host router (see app.ts).

import { Hono, type Context } from 'hono';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { z } from 'zod';
import { blankPbm, cropSquare, parsePbm, pbmToPng, setPixel } from '@little-camera/pbm';
import { appUrl, blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { newSubscriberMail } from '../emails/index.js';
import { batteryPercent } from '../lib/battery.js';
import { formatMeta } from '../lib/dates.js';
import { queueEmail } from '../lib/email.js';
import { currentOwner, currentSubscriber, setSubscriberCookie } from '../lib/session.js';
import { verifyToken } from '../lib/tokens.js';
import { sendFreshLink } from '../lib/welcome.js';
import { findCameraByProfile } from '../repo/cameras.js';
import { commentsForPhotos, insertComment } from '../repo/comments.js';
import { countFeedPhotos, feedPage, findPhotoById, findPhotoByPublicId, newestFeedPhoto, type PhotoRow } from '../repo/photos.js';
import type { ProfileRow } from '../repo/profiles.js';
import { createSubscriber, findSubscriberByEmail, findSubscriberById, setSubscriberName } from '../repo/subscribers.js';
import { Page, Simple } from '../views/layout.js';
import { Article, BlogHeader, BlogScript, LoadMore, Notice, SubscribeRow, blogTitle, type ArticleData, type Viewer } from '../views/blog.js';

type BlogEnv = { Bindings: { profile: ProfileRow } };

const PAGE_SIZE = 10;
const emailSchema = z.string().trim().toLowerCase().email().max(200);

export function blogRoutes(env: Env): Hono<BlogEnv> {
  const app = new Hono<BlogEnv>();

  // Who is looking? The owner (apex cookie, production only), an approved
  // subscriber (blog cookie), or nobody.
  async function viewerOf(c: Context<BlogEnv>, profile: ProfileRow): Promise<Viewer> {
    const owner = await currentOwner(c, env);
    if (owner && owner.id === profile.id) return { kind: 'owner' };
    const sub = await currentSubscriber(c, env, profile.id);
    if (sub) return { kind: 'subscriber', id: sub.id, name: sub.name };
    return { kind: 'anonymous' };
  }

  function battery(profile: ProfileRow): number {
    const camera = findCameraByProfile(env.db, profile.id);
    const now = env.now();
    return camera ? batteryPercent(camera.id, camera.bound_at ?? now, now) : 100;
  }

  function articles(profile: ProfileRow, photos: PhotoRow[]): ArticleData[] {
    const comments = commentsForPhotos(env.db, photos.map((p) => p.id), profile.name);
    return photos.map((photo) => ({
      photo,
      ...formatMeta(photo.captured_at, env.config.displayTz),
      comments: comments.get(photo.id) ?? [],
    }));
  }

  const head = (profile: ProfileRow) => (
    <>
      <meta property="og:type" content="website" />
      <meta property="og:site_name" content={blogTitle(profile.name)} />
      <meta property="og:title" content={blogTitle(profile.name)} />
      <meta property="og:description" content="Photos from a little camera. New ones arrive on their own." />
      <meta property="og:url" content={blogUrl(env.config, profile.handle) + '/'} />
      <meta property="og:image" content={blogUrl(env.config, profile.handle) + '/avatar.png'} />
      <link rel="icon" href="/avatar.png" type="image/png" />
    </>
  );

  /** The full feed for owner/subscribers, one page, optionally starting at a photo. */
  function renderFeed(profile: ProfileRow, viewer: Viewer, opts: { before?: { uploadedAt: number; id: number }; notice?: string; startAt?: PhotoRow }) {
    // Starting at a photo = "everything from that photo on", so a page can
    // open on the picture an email linked to without a scroll-to script.
    const before = opts.startAt ? { uploadedAt: opts.startAt.uploaded_at, id: opts.startAt.id + 1 } : opts.before;
    const photos = feedPage(env.db, profile.id, PAGE_SIZE + 1, before);
    const hasMore = photos.length > PAGE_SIZE;
    const page = photos.slice(0, PAGE_SIZE);
    const last = page[page.length - 1];
    const moreHref = hasMore && last ? `/?before=${last.uploaded_at}&bid=${last.id}` : null;
    return (
      <Page title={blogTitle(profile.name)} description="Photos from a little camera. New ones arrive on their own." head={head(profile)}>
        <BlogHeader name={profile.name} battery={battery(profile)} viewer={viewer} meUrl={appUrl(env.config, '/me')} />
        {opts.notice ? <Notice text={opts.notice} /> : null}
        <main id="feed">
          {page.length === 0 ? <div class="teaser">No pictures yet</div> : null}
          {articles(profile, page).map((a) => (
            <Article a={a} viewer={viewer} />
          ))}
        </main>
        {page.length > 0 ? <LoadMore href={moreHref} /> : null}
        <BlogScript />
      </Page>
    );
  }

  /** What a visitor without a link sees: header, join form, the newest picture, and a count. */
  function renderTeaser(profile: ProfileRow, notice?: string) {
    const newest = newestFeedPhoto(env.db, profile.id);
    const total = countFeedPhotos(env.db, profile.id);
    const more = Math.max(0, total - 1);
    const viewer: Viewer = { kind: 'anonymous' };
    return (
      <Page title={blogTitle(profile.name)} description="Photos from a little camera. New ones arrive on their own." head={head(profile)}>
        <BlogHeader name={profile.name} battery={battery(profile)} viewer={viewer} meUrl={appUrl(env.config, '/me')} />
        {notice ? <Notice text={notice} /> : null}
        <SubscribeRow open={!notice} />
        <main id="feed">
          {newest ? articles(profile, [newest]).map((a) => <Article a={a} viewer={viewer} />) : null}
        </main>
        <div class="teaser">
          {!newest
            ? 'No pictures yet'
            : more === 0
              ? 'More pictures for subscribers, soon'
              : `${more} more ${more === 1 ? 'picture' : 'pictures'} for subscribers`}
        </div>
        <BlogScript />
      </Page>
    );
  }

  const NOTICES: Record<string, string> = {
    sent: 'Thanks. The owner will let you in.',
    pending: 'Still waiting for the owner to let you in.',
    already: 'You’re already in. Use the link from your email.',
  };

  // Accept a subscriber token on the root too (welcome links for blogs
  // without pictures yet).
  app.get('/', async (c) => {
    const profile = c.env.profile;
    const t = c.req.query('t');
    if (t) {
      const v = verifyToken(env, t, 'subscriber');
      if (v.ok) {
        const sub = v.row.subscriber_id !== null ? findSubscriberById(env.db, v.row.subscriber_id) : null;
        if (sub && sub.profile_id === profile.id && sub.status === 'approved') {
          await setSubscriberCookie(c, env, sub.id, v.row.expires_at - env.now());
          return c.redirect('/', 303);
        }
      }
      return c.html(expiredPage(v.ok ? null : v.expired?.subscriber_id ?? null), 410);
    }
    const viewer = await viewerOf(c, profile);
    const notice = NOTICES[c.req.query('joined') ?? ''];
    if (viewer.kind === 'anonymous') return c.html(renderTeaser(profile, notice));
    const beforeAt = Number(c.req.query('before'));
    const beforeId = Number(c.req.query('bid'));
    const before = beforeAt > 0 && beforeId > 0 ? { uploadedAt: beforeAt, id: beforeId } : undefined;
    return c.html(renderFeed(profile, viewer, { before, notice }));
  });

  function expiredPage(subscriberId: number | null) {
    const sub = subscriberId !== null ? findSubscriberById(env.db, subscriberId) : null;
    return (
      <Simple title="This link has expired" heading="This link has expired">
        <p>Links work for 48 hours. Ask for a fresh one and it is in your inbox in a moment.</p>
        <form method="post" action="/request-link" class="row">
          <label class="field">
            <span>Email</span>
            <input type="email" name="email" value={sub?.email ?? ''} required autocomplete="email" />
          </label>
          <button class="btn" type="submit">
            Send me a fresh link
          </button>
        </form>
      </Simple>
    );
  }

  // A photo link. With ?t= it verifies the token and sets the cookie; then
  // (or with an existing cookie) it shows the feed starting at that photo.
  app.get('/p/:public_id', async (c) => {
    const profile = c.env.profile;
    const photo = findPhotoByPublicId(env.db, c.req.param('public_id'));
    if (!photo || photo.profile_id !== profile.id || photo.kind === 'verification') return c.notFound();
    const t = c.req.query('t');
    if (t) {
      const v = verifyToken(env, t, 'subscriber');
      if (v.ok) {
        const sub = v.row.subscriber_id !== null ? findSubscriberById(env.db, v.row.subscriber_id) : null;
        if (sub && sub.profile_id === profile.id && sub.status === 'approved') {
          await setSubscriberCookie(c, env, sub.id, v.row.expires_at - env.now());
          return c.redirect(`/p/${photo.public_id}`, 303);
        }
      }
      return c.html(expiredPage(v.ok ? null : v.expired?.subscriber_id ?? null), 410);
    }
    const viewer = await viewerOf(c, profile);
    if (viewer.kind === 'anonymous') return c.redirect('/', 303);
    return c.html(renderFeed(profile, viewer, { startAt: photo }));
  });

  // Always the same answer, so the form cannot be used to probe who subscribes.
  app.post('/request-link', async (c) => {
    const profile = c.env.profile;
    const form = await c.req.parseBody();
    const email = emailSchema.safeParse(form.email);
    if (email.success) {
      const sub = findSubscriberByEmail(env.db, profile.id, email.data);
      if (sub && sub.status === 'approved') sendFreshLink(env, profile, sub);
    }
    return c.html(
      <Simple title="Check your email" heading="Check your email">
        <p>If that address is on the list, a fresh link is on its way.</p>
      </Simple>,
    );
  });

  // Join form. Creates a pending subscriber and tells the owner.
  app.post('/subscribe', async (c) => {
    const profile = c.env.profile;
    const wantsJson = (c.req.header('accept') ?? '').includes('application/json');
    const raw = (c.req.header('content-type') ?? '').includes('application/json')
      ? await c.req.json().catch(() => ({}))
      : await c.req.parseBody();
    const email = emailSchema.safeParse((raw as { email?: unknown }).email);
    if (!email.success) {
      return wantsJson ? c.json({ error: 'bad_email' }, 400) : c.redirect('/#join', 303);
    }
    let state: 'sent' | 'pending' | 'already';
    const existing = findSubscriberByEmail(env.db, profile.id, email.data);
    if (existing) {
      // Blocked people get the same answer as pending ones.
      state = existing.status === 'approved' ? 'already' : 'pending';
    } else {
      createSubscriber(env.db, { profileId: profile.id, email: email.data, name: null, status: 'pending', addedBy: 'self', now: env.now() });
      queueEmail(env, profile.email, newSubscriberMail({ ownerName: profile.name, email: email.data, meUrl: appUrl(env.config, '/me') }));
      state = 'sent';
    }
    return wantsJson ? c.json({ state }) : c.redirect(`/?joined=${state}`, 303);
  });

  // Comments: subscribers (named on first comment) and the owner.
  app.post('/p/:public_id/comments', async (c) => {
    const profile = c.env.profile;
    const photo = findPhotoByPublicId(env.db, c.req.param('public_id'));
    if (!photo || photo.profile_id !== profile.id || photo.kind === 'verification') return c.notFound();
    const viewer = await viewerOf(c, profile);
    const wantsJson = (c.req.header('accept') ?? '').includes('application/json');
    if (viewer.kind === 'anonymous') return wantsJson ? c.json({ error: 'unauthorized' }, 401) : c.redirect('/', 303);
    const form = await c.req.parseBody();
    const body = String(form.body ?? '').trim();
    if (!body || body.length > 1000) return wantsJson ? c.json({ error: 'bad_body' }, 400) : c.redirect(`/p/${photo.public_id}`, 303);

    let name = profile.name;
    let subscriberId: number | null = null;
    if (viewer.kind === 'subscriber') {
      subscriberId = viewer.id;
      if (viewer.name) {
        name = viewer.name;
      } else {
        const given = String(form.name ?? '').trim().slice(0, 60);
        if (!given) return wantsJson ? c.json({ error: 'name_required' }, 400) : c.redirect(`/p/${photo.public_id}`, 303);
        setSubscriberName(env.db, viewer.id, given);
        name = given;
      }
    }
    insertComment(env.db, { photoId: photo.id, subscriberId, body, now: env.now() });
    return wantsJson ? c.json({ name, body }) : c.redirect(`/p/${photo.public_id}`, 303);
  });

  // The picture itself: the stored PBM rendered as a 1-bit PNG. Content is
  // addressed by an unguessable id and never changes, so cache forever.
  app.get('/photos/:file', (c) => {
    const profile = c.env.profile;
    const m = c.req.param('file').match(/^([a-z0-9]{12})\.png$/);
    const photo = m ? findPhotoByPublicId(env.db, m[1]) : null;
    if (!photo || photo.profile_id !== profile.id || photo.kind === 'verification') return c.notFound();
    const etag = `"${photo.sha256}"`;
    if (c.req.header('if-none-match') === etag) return c.body(null, 304);
    const pbm = parsePbm(new Uint8Array(readFileSync(join(env.config.dataDir, photo.path))));
    return c.body(pbmToPng(pbm), 200, {
      'content-type': 'image/png',
      'cache-control': 'public, max-age=31536000, immutable',
      etag,
    });
  });

  // 240×240 centre crop of the avatar photo, or a 2 px checker until there is one.
  app.get('/avatar.png', (c) => {
    const profile = c.env.profile;
    const avatar = profile.avatar_photo_id !== null ? findPhotoById(env.db, profile.avatar_photo_id) : null;
    let png: Uint8Array<ArrayBuffer>;
    let etag: string;
    if (avatar) {
      const pbm = parsePbm(new Uint8Array(readFileSync(join(env.config.dataDir, avatar.path))));
      png = pbmToPng(cropSquare(pbm, 240));
      etag = `"${avatar.sha256}"`;
    } else {
      png = pbmToPng(checker(240));
      etag = '"checker"';
    }
    if (c.req.header('if-none-match') === etag) return c.body(null, 304);
    return c.body(png, 200, { 'content-type': 'image/png', 'cache-control': 'public, max-age=3600', etag });
  });

  return app;
}

/** A square filled with the 2 px checker: the default profile picture. */
function checker(size: number) {
  const pbm = blankPbm(size, size);
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) if (((x >> 1) + (y >> 1)) % 2 === 0) setPixel(pbm, x, y, true);
  }
  return pbm;
}
