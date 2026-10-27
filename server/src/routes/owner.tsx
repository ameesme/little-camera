import { Hono, type Context } from 'hono';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { z } from 'zod';
import { parsePbm, pbmToPng, randomCode } from '@little-camera/pbm';
import { apexUrl, appUrl, blogUrl } from '../config.js';
import * as subscribers from '../core/subscribers.js';
import type { Env } from '../env.js';
import { loginLinkMail, verifyEmailMail } from '../emails/index.js';
import { formatFull } from '../lib/dates.js';
import { queueEmail } from '../lib/email.js';
import { HANDLE, RESERVED_HANDLES } from '../lib/handles.js';
import { verificationQrSvg } from '../lib/qr.js';
import { clearOwnerCookie, currentOwner, setOwnerCookie } from '../lib/session.js';
import { createToken, verifyToken } from '../lib/tokens.js';
import { findCameraByBlog } from '../repo/cameras.js';
import { insertCode } from '../repo/codes.js';
import { findOutbox, listOutbox } from '../repo/outbox.js';
import { findPhotoById } from '../repo/photos.js';
import {
  createBlog,
  findBlogByEmail,
  findBlogByHandle,
  markEmailVerified,
  setAvatarRequested,
  type BlogRow,
} from '../repo/blogs.js';
import { listSubscribers } from '../repo/subscribers.js';
import {
  CheckEmail,
  ExpiredLink,
  Landing,
  LoginForm,
  Mailbox,
  MailView,
  MyBlogPage,
  RegisterForm,
  VerificationPage,
  u,
} from '../views/owner.js';

const registerSchema = z.object({
  name: z.string().trim().min(1, 'Name is required').max(60, 'Name is too long'),
  email: z.string().trim().toLowerCase().email('That email does not look right').max(200),
  handle: z
    .string()
    .trim()
    .toLowerCase()
    .regex(HANDLE, 'Handle: 2–31 characters, a–z, 0–9 and dashes, starting with a letter or digit')
    .refine((h) => !RESERVED_HANDLES.has(h), 'That handle is reserved'),
});

const emailSchema = z.string().trim().toLowerCase().email().max(200);


export function ownerRoutes(env: Env): Hono {
  const app = new Hono();
  const dev = env.config.nodeEnv !== 'production';

  const owner = (c: Context): Promise<BlogRow | null> => currentOwner(c, env);

  app.get('/', (c) => c.html(<Landing />));

  app.get('/register', (c) => c.html(<RegisterForm baseDomain={env.config.baseDomain} />));

  app.post('/register', async (c) => {
    const form = await c.req.parseBody();
    const values = { name: String(form.name ?? ''), email: String(form.email ?? ''), handle: String(form.handle ?? '') };
    const reject = (message: string, status: 400 | 409) =>
      c.html(<RegisterForm baseDomain={env.config.baseDomain} values={values} error={message} />, status);

    const parsed = registerSchema.safeParse(values);
    if (!parsed.success) return reject(parsed.error.issues[0].message, 400);
    const { name, email, handle } = parsed.data;
    if (findBlogByHandle(env.db, handle)) return reject('That handle is taken', 409);
    if (findBlogByEmail(env.db, email)) return reject('That email already has a little camera. Sign in instead.', 409);

    const blog = createBlog(env.db, { handle, name, email, now: env.now() });
    const { token } = createToken(env, { kind: 'email_verify', blogId: blog.id });
    queueEmail(env, email, verifyEmailMail({ name, url: appUrl(env.config, `/verify-email?t=${token}`) }));
    return c.html(<CheckEmail dev={dev} />);
  });

  app.get('/verify-email', async (c) => {
    const verified = verifyToken(env, c.req.query('t'), 'email_verify');
    if (!verified.ok) return c.html(<ExpiredLink />, 410);
    markEmailVerified(env.db, verified.row.blog_id, env.now());
    await setOwnerCookie(c, env, verified.row.blog_id);
    return c.redirect(u('/me'), 303);
  });

  // The emailed link is registered before the form so a `?t=` request never falls through to an empty page.
  app.get('/login', async (c, next) => {
    const token = c.req.query('t');
    if (!token) return next();
    const verified = verifyToken(env, token, 'owner_login');
    if (!verified.ok) return c.html(<ExpiredLink />, 410);
    await setOwnerCookie(c, env, verified.row.blog_id);
    return c.redirect(u('/me'), 303);
  });

  app.get('/login', (c) => c.html(<LoginForm />));

  app.post('/login', async (c) => {
    const form = await c.req.parseBody();
    const email = emailSchema.safeParse(form.email);
    const blog = email.success ? findBlogByEmail(env.db, email.data) : null;
    if (blog) {
      // An unverified blog gets a verify link instead: it signs them in too, and marks the email verified.
      const kind = blog.email_verified_at ? 'owner_login' : 'email_verify';
      const { token } = createToken(env, { kind, blogId: blog.id });
      const path = kind === 'owner_login' ? '/login' : '/verify-email';
      const url = appUrl(env.config, `${path}?t=${token}`);
      const mail = kind === 'owner_login' ? loginLinkMail : verifyEmailMail;
      queueEmail(env, blog.email, mail({ name: blog.name, url }));
    }
    return c.html(<CheckEmail dev={dev} />);
  });

  app.post('/logout', (c) => {
    clearOwnerCookie(c, env);
    return c.redirect(u(), 303);
  });

  app.get('/me', async (c) => {
    const blog = await owner(c);
    if (!blog) return c.redirect(u('/login'), 303);
    const camera = findCameraByBlog(env.db, blog.id);
    if (camera) {
      return c.html(
        <MyBlogPage
          blog={blog}
          blogUrl={blogUrl(env.config, blog.handle)}
          shortCode={camera.short_code}
          subscribers={listSubscribers(env.db, blog.id)}
        />,
      );
    }
    const code = insertCode(env.db, randomCode(6), blog.id, env.now());
    return c.html(
      <VerificationPage
        code={code.code}
        svg={await verificationQrSvg(code.code)}
        email={blog.email}
        apexUrl={apexUrl(env.config)}
        blogUrl={blogUrl(env.config, blog.handle)}
      />,
    );
  });

  app.get('/me/status', async (c) => {
    const blog = await owner(c);
    if (!blog) return c.json({ error: 'unauthorized' }, 401);
    const camera = findCameraByBlog(env.db, blog.id);
    return c.json({ bound: !!camera, url: camera ? blogUrl(env.config, blog.handle) : null });
  });

  app.post('/me/request-avatar', async (c) => {
    const blog = await owner(c);
    if (!blog) return c.redirect(u('/login'), 303);
    setAvatarRequested(env.db, blog.id, env.now());
    return c.redirect(u('/me'), 303);
  });

  app.post('/me/subscribers', async (c) => {
    const blog = await owner(c);
    if (!blog) return c.redirect(u('/login'), 303);
    const form = await c.req.parseBody();
    const email = emailSchema.safeParse(form.email);
    if (email.success) {
      const name = String(form.name ?? '').trim().slice(0, 60) || null;
      subscribers.add(env, blog, email.data, name);
    }
    return c.redirect(u('/me'), 303);
  });

  app.post('/me/subscribers/:id/:action{approve|block}', async (c) => {
    const blog = await owner(c);
    if (!blog) return c.redirect(u('/login'), 303);
    const subscriber = subscribers.find(env, blog, Number(c.req.param('id')));
    if (subscriber) {
      if (c.req.param('action') === 'approve') subscribers.approve(env, subscriber);
      else subscribers.block(env, subscriber);
    }
    return c.redirect(u('/me'), 303);
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
          html={mail.html_body.replace('cid:photo', `/dev/mailbox/${mail.id}/photo.png`)}
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
