import { Hono } from 'hono';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { parsePbm, pbmToPng } from '@little-camera/pbm';
import { apexUrl, blogUrl } from '../config.js';
import { confirmEmail } from '../core/blogs.js';
import { approveClaim } from '../core/cameras.js';
import { hashToken } from '../lib/tokens.js';
import { findTokenByHash } from '../repo/tokens.js';
import { findCamera } from '../repo/cameras.js';
import type { Env } from '../env.js';
import { formatFull } from '../lib/dates.js';
import { findOutbox, listOutbox } from '../repo/outbox.js';
import { findPhotoById } from '../repo/photos.js';
import { findBlogById } from '../repo/blogs.js';
import { ClaimDone, ClaimPrompt, EmailConfirmed, ExpiredLink, Mailbox, MailView, u } from '../views/owner.js';

/** The web does no management: the app owns all of it (§4.9). These are the pages an emailed link opens. */
export function ownerRoutes(env: Env): Hono {
  const app = new Hono();
  const dev = env.config.nodeEnv !== 'production';
  const hostOf = (handle: string) => blogUrl(env.config, handle).replace(/^https?:\/\//, '');

  // Nothing to land on: the app does the managing and the landing page owns the apex.
  app.get('/', (c) => c.redirect(apexUrl(env.config), 302));

  app.get('/verify-email', (c) => {
    const blog = confirmEmail(env, c.req.query('t'));
    return blog ? c.html(<EmailConfirmed blogUrl={blogUrl(env.config, blog.handle)} />) : c.html(<ExpiredLink />, 410);
  });

  // Shown before anything is granted, because the link lands in a mail client that may have prefetched it.
  app.get('/claim', (c) => {
    const token = c.req.query('t') ?? '';
    const row = findTokenByHash(env.db, hashToken(token));
    const camera = row?.camera_id ? findCamera(env.db, row.camera_id) : null;
    const blog = row ? findBlogById(env.db, row.blog_id) : null;
    if (!row || row.kind !== 'device_claim' || row.expires_at <= env.now() || !camera || !blog) {
      return c.html(<ExpiredLink />, 410);
    }
    return c.html(<ClaimPrompt blogName={hostOf(blog.handle)} shortCode={camera.short_code} token={token} />);
  });

  app.post('/claim', async (c) => {
    const claimed = approveClaim(env, String((await c.req.parseBody()).t ?? ''));
    return claimed
      ? c.html(<ClaimDone blogName={hostOf(claimed.blog.handle)} />)
      : c.html(<ExpiredLink />, 410);
  });

  if (dev) {
    app.get('/dev/mailbox', (c) =>
      c.html(
        <Mailbox
          rows={listOutbox(env.db).map((m) => ({
            id: m.id,
            subject: m.subject,
            to_email: m.to_email,
            status: m.status,
            when: formatFull(m.created_at, env.config.displayTz),
          }))}
        />,
      ),
    );

    app.get('/dev/mailbox/:id', (c) => {
      const mail = findOutbox(env.db, Number(c.req.param('id')));
      if (!mail) return c.notFound();
      const linked = mail.text_body
        .split(/(https?:\/\/\S+)/)
        .map((part, i) => (i % 2 ? <a href={part}>{part}</a> : part));
      return c.html(
        <MailView
          subject={mail.subject}
          to={mail.to_email}
          status={mail.status}
          text={linked}
          html={mail.html_body.replace('cid:photo', `${u(`/dev/mailbox/${mail.id}`)}/photo.png`)}
        />,
      );
    });

    app.get('/dev/mailbox/:id/photo.png', (c) => {
      const mail = findOutbox(env.db, Number(c.req.param('id')));
      const photo = mail?.photo_id ? findPhotoById(env.db, mail.photo_id) : null;
      if (!photo) return c.notFound();
      const pbm = parsePbm(new Uint8Array(readFileSync(join(env.config.dataDir, photo.path))));
      return c.body(pbmToPng(pbm), 200, { 'content-type': 'image/png' });
    });
  }

  return app;
}
