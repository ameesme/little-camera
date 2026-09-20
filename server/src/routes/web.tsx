// The apex: landing, registration, sign-in links, /me (the verification
// picture or the profile page) and the dev mailbox.

import { Hono, type Context } from 'hono';
import { raw } from 'hono/html';
import { z } from 'zod';
import { randomCode } from '@little-camera/pbm';
import { APP_PREFIX, apexUrl, appUrl, blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { loginLinkMail, verifyEmailMail } from '../emails/index.js';
import { formatFull } from '../lib/dates.js';
import { queueEmail } from '../lib/email.js';
import { verificationQrSvg } from '../lib/qr.js';
import { clearOwnerCookie, currentOwner, setOwnerCookie } from '../lib/session.js';
import { createToken, verifyToken } from '../lib/tokens.js';
import { sendWelcome } from '../lib/welcome.js';
import { bindCamera, findCameraByProfile, findCameraByShortCode } from '../repo/cameras.js';
import { insertCode } from '../repo/codes.js';
import { findOutbox, listOutbox } from '../repo/outbox.js';
import { findPhotoById, lastUploadAt } from '../repo/photos.js';
import { createProfile, findProfileByEmail, findProfileByHandle, markEmailVerified, setAvatarRequested, type ProfileRow } from '../repo/profiles.js';
import {
  approveSubscriber,
  blockSubscriber,
  createSubscriber,
  findSubscriberByEmail,
  findSubscriberById,
  listSubscribers,
} from '../repo/subscribers.js';
import { Page, Simple } from '../views/layout.js';
import { HANDLE, RESERVED_HANDLES } from '../lib/handles.js';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { parsePbm, pbmToPng } from '@little-camera/pbm';

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

// These pages are mounted under APP_PREFIX, not at the apex root — the root
// belongs to the landing page (config.ts). Every link on them is absolute, so
// they all go through here rather than each one carrying the prefix.
const u = (path = '') => `${APP_PREFIX}${path}`;

export function webRoutes(env: Env): Hono {
  const app = new Hono();
  const dev = env.config.nodeEnv !== 'production';

  // In-memory rate limit for the typed-code fallback: 10 tries per hour per
  // profile. Lives with the app instance (one process = one instance), so a
  // restart forgets it; that is fine for a brute-force guard on a 30-bit code.
  const linkAttempts = new Map<number, number[]>();
  function allowLinkAttempt(profileId: number, now: number): boolean {
    const recent = (linkAttempts.get(profileId) ?? []).filter((t) => t > now - 3600);
    if (recent.length >= 10) return false;
    recent.push(now);
    linkAttempts.set(profileId, recent);
    return true;
  }

  app.get('/', (c) =>
    c.html(
      <Simple title="Little camera" heading="Little camera">
        <p>
          A small black-and-white camera that posts to its own tiny blog. Register, photograph the code on your phone with
          the camera, and every picture you send ends up on your page for the people you let in.
        </p>
        <p>
          <a class="btn" href={u('/register')}>
            Register
          </a>{' '}
          <a class="btn" href={u('/login')}>
            Sign in
          </a>
        </p>
      </Simple>,
    ),
  );

  // --- registration -------------------------------------------------

  const RegisterForm = (p: { values?: Record<string, string>; error?: string }) => (
    <Simple title="Register" heading="Register your little camera">
      {p.error ? <div class="err">{p.error}</div> : null}
      <form method="post" action={u('/register')}>
        <label class="field">
          <span>Name</span>
          <input name="name" value={p.values?.name ?? ''} required maxlength={60} autocomplete="name" />
        </label>
        <label class="field">
          <span>Email</span>
          <input name="email" type="email" value={p.values?.email ?? ''} required autocomplete="email" />
        </label>
        <label class="field">
          <span>Handle (your blog will be handle.{env.config.baseDomain})</span>
          <input name="handle" value={p.values?.handle ?? ''} required pattern="[a-z0-9][a-z0-9-]{1,30}" autocapitalize="off" autocomplete="off" />
        </label>
        <button class="btn" type="submit">
          Register
        </button>
      </form>
      <p style="margin-top:18px">
        Already registered? <a href={u('/login')}>Sign in</a>.
      </p>
    </Simple>
  );

  app.get('/register', (c) => c.html(<RegisterForm />));

  app.post('/register', async (c) => {
    const form = await c.req.parseBody();
    const values = { name: String(form.name ?? ''), email: String(form.email ?? ''), handle: String(form.handle ?? '') };
    const parsed = registerSchema.safeParse(values);
    if (!parsed.success) return c.html(<RegisterForm values={values} error={parsed.error.issues[0].message} />, 400);
    const { name, email, handle } = parsed.data;
    if (findProfileByHandle(env.db, handle)) return c.html(<RegisterForm values={values} error="That handle is taken" />, 409);
    if (findProfileByEmail(env.db, email)) {
      return c.html(<RegisterForm values={values} error="That email already has a little camera. Sign in instead." />, 409);
    }
    const profile = createProfile(env.db, { handle, name, email, now: env.now() });
    const { token } = createToken(env, { kind: 'email_verify', profileId: profile.id });
    queueEmail(env, email, verifyEmailMail({ name, url: appUrl(env.config, `/verify-email?t=${token}`) }));
    return c.html(<CheckEmail />);
  });

  const CheckEmail = () => (
    <Simple title="Check your email" heading="Check your email">
      <p>We sent you a link. Open it on your phone: that is where the next step happens.</p>
      {dev ? (
        <p>
          Development: <a href={u('/dev/mailbox')}>open the dev mailbox</a>.
        </p>
      ) : null}
    </Simple>
  );

  app.get('/verify-email', async (c) => {
    const v = verifyToken(env, c.req.query('t'), 'email_verify');
    if (!v.ok) return c.html(<ExpiredLink />, 410);
    markEmailVerified(env.db, v.row.profile_id, env.now());
    await setOwnerCookie(c, env, v.row.profile_id);
    return c.redirect(u('/me'), 303);
  });

  const ExpiredLink = () => (
    <Simple title="This link has expired" heading="This link has expired">
      <p>
        <a href={u('/login')}>Ask for a new one</a>.
      </p>
    </Simple>
  );

  // --- sign in --------------------------------------------------------

  // The emailed link. Registered before the form so a ?t= request never
  // falls through to the empty form (Hono stops at the first match).
  app.get('/login', async (c, next) => {
    const t = c.req.query('t');
    if (!t) return next();
    const v = verifyToken(env, t, 'owner_login');
    if (!v.ok) return c.html(<ExpiredLink />, 410);
    await setOwnerCookie(c, env, v.row.profile_id);
    return c.redirect(u('/me'), 303);
  });

  app.get('/login', (c) =>
    c.html(
      <Simple title="Sign in" heading="Sign in">
        <form method="post" action={u('/login')} class="row">
          <label class="field">
            <span>Email</span>
            <input name="email" type="email" required autocomplete="email" />
          </label>
          <button class="btn" type="submit">
            Send link
          </button>
        </form>
      </Simple>,
    ),
  );

  app.post('/login', async (c) => {
    const form = await c.req.parseBody();
    const email = emailSchema.safeParse(form.email);
    if (email.success) {
      const profile = findProfileByEmail(env.db, email.data);
      if (profile) {
        // An unverified profile gets a verify link instead: same effect, and
        // it also marks the email as verified on click.
        const kind = profile.email_verified_at ? 'owner_login' : 'email_verify';
        const { token } = createToken(env, { kind, profileId: profile.id });
        const url = appUrl(env.config, `/${kind === 'owner_login' ? 'login' : 'verify-email'}?t=${token}`);
        queueEmail(env, profile.email, kind === 'owner_login' ? loginLinkMail({ name: profile.name, url }) : verifyEmailMail({ name: profile.name, url }));
      }
    }
    return c.html(<CheckEmail />);
  });

  app.post('/logout', (c) => {
    clearOwnerCookie(c, env);
    return c.redirect(u(), 303);
  });

  // --- /me ------------------------------------------------------------

  async function requireOwner(c: Context): Promise<ProfileRow | null> {
    return currentOwner(c, env);
  }

  app.get('/me', async (c) => {
    const profile = await requireOwner(c);
    if (!profile) return c.redirect(u('/login'), 303);
    const camera = findCameraByProfile(env.db, profile.id);
    if (!camera) return c.html(await verificationPage(profile));
    return c.html(profilePage(profile, camera.short_code));
  });

  /**
   * The verification picture: full-viewport white, the QR as big as the
   * screen allows, two statements in the blog's uppercase style, the
   * code in small type, and the typed-code fallback.
   */
  async function verificationPage(profile: ProfileRow) {
    const code = insertCode(env.db, randomCode(6), profile.id, env.now());
    const svg = await verificationQrSvg(code.code);
    return (
      <Page title="This is my little camera" wrap={false}>
        <div class="verify" id="verify">
          <p class="stmt">This is my little camera</p>
          <div class="qr" aria-label={`QR code LC:${code.code}`}>{raw(svg)}</div>
          <p class="stmt">Take a picture of this and press send</p>
          <p class="code">LC:{code.code}</p>
          <div class="small">
            Camera not recognised? Type the code from the app.
            <form method="post" action={u('/me/link-camera')}>
              <input name="short_code" placeholder="ABC123" maxlength={6} pattern="[A-Za-z2-9]{6}" required autocapitalize="characters" autocomplete="off" />
              <button class="btn" type="submit">
                Link
              </button>
            </form>
          </div>
          <p class="small">
            <a href={apexUrl(env.config)}>Little camera</a> · {profile.email} ·{' '}
            <form method="post" action={u('/logout')} style="display:inline">
              <button type="submit" style="text-decoration:underline">
                sign out
              </button>
            </form>
          </p>
        </div>
        <script>
          {raw(`
(function(){
  var url=${JSON.stringify(blogUrl(env.config, profile.handle))};
  function tick(){
    fetch(${JSON.stringify(u('/me/status'))},{headers:{accept:'application/json'}}).then(function(r){return r.json()}).then(function(j){
      if(!j.bound)return;
      clearInterval(timer);
      var v=document.getElementById('verify');
      v.innerHTML='<p class="stmt">Linked</p><p class="url"><a href="'+j.url+'">'+j.url+'</a></p><p class="small"><a href="'+${JSON.stringify(u('/me'))}+'">Your page</a></p>';
    }).catch(function(){});
  }
  var timer=setInterval(tick,3000);
})();`)}
        </script>
      </Page>
    );
  }

  app.get('/me/status', async (c) => {
    const profile = await requireOwner(c);
    if (!profile) return c.json({ error: 'unauthorized' }, 401);
    const camera = findCameraByProfile(env.db, profile.id);
    return c.json({ bound: !!camera, url: camera ? blogUrl(env.config, profile.handle) : null });
  });

  // Typed short code: only for a camera that uploaded something in the
  // last 10 minutes (proof that the person has the camera in hand).
  app.post('/me/link-camera', async (c) => {
    const profile = await requireOwner(c);
    if (!profile) return c.redirect(u('/login'), 303);
    if (findCameraByProfile(env.db, profile.id)) return c.redirect(u('/me'), 303);
    const now = env.now();
    const form = await c.req.parseBody();
    const code = String(form.short_code ?? '').trim().toUpperCase().replace(/[^A-Z2-9]/g, '');
    const fail = (why: string) =>
      c.html(
        <Simple title="Not linked" heading="Not linked">
          <p>{why}</p>
          <p>
            <a href={u('/me')}>Back to the code</a>
          </p>
        </Simple>,
        400,
      );
    if (!allowLinkAttempt(profile.id, now)) return fail('Too many tries. Wait an hour, or photograph the code instead.');
    const camera = code.length === 6 ? findCameraByShortCode(env.db, code) : null;
    if (!camera || camera.profile_id !== null) return fail('No camera with that code is waiting to be linked.');
    const lastUpload = lastUploadAt(env.db, camera.id);
    if (lastUpload === null || lastUpload < now - 600) {
      return fail('That camera has not uploaded anything in the last 10 minutes. Take a picture, press send, and try again.');
    }
    bindCamera(env.db, camera.id, profile.id, now);
    return c.redirect(u('/me'), 303);
  });

  function profilePage(profile: ProfileRow, shortCode: string) {
    const subs = listSubscribers(env.db, profile.id);
    const url = blogUrl(env.config, profile.handle);
    return (
      <Simple title="Your little camera" heading="Your little camera">
        <p>
          Your blog: <a href={url}>{url}</a>
        </p>
        <p>
          Camera <code>{shortCode}</code> is linked.{' '}
          {profile.avatar_requested_at
            ? 'The next picture you take becomes your profile picture.'
            : profile.avatar_photo_id
              ? 'You have a profile picture.'
              : 'No profile picture yet.'}
        </p>
        <form method="post" action={u('/me/request-avatar')}>
          <button class="btn" type="submit">
            {profile.avatar_photo_id ? 'Replace profile picture' : 'Request profile picture'}
          </button>
        </form>

        <h1 style="margin-top:28px">Subscribers</h1>
        {subs.length === 0 ? <p>Nobody yet.</p> : null}
        <ul class="list">
          {subs.map((s) => (
            <li>
              <span>
                {s.name ? `${s.name} · ` : ''}
                {s.email}
                <span class="tag">{s.status}</span>
              </span>
              <span>
                {s.status !== 'approved' ? (
                  <form method="post" action={`/me/subscribers/${s.id}/approve`}>
                    <button class="btn" type="submit">
                      Approve
                    </button>
                  </form>
                ) : null}{' '}
                {s.status !== 'blocked' ? (
                  <form method="post" action={`/me/subscribers/${s.id}/block`}>
                    <button class="btn" type="submit">
                      Block
                    </button>
                  </form>
                ) : null}
              </span>
            </li>
          ))}
        </ul>
        <form method="post" action={u('/me/subscribers')} class="row">
          <label class="field">
            <span>Email</span>
            <input name="email" type="email" required />
          </label>
          <label class="field">
            <span>Name (optional)</span>
            <input name="name" maxlength={60} />
          </label>
          <button class="btn" type="submit">
            Add
          </button>
        </form>
        <form method="post" action={u('/logout')} style="margin-top:28px">
          <button class="btn" type="submit">
            Sign out
          </button>
        </form>
      </Simple>
    );
  }

  app.post('/me/request-avatar', async (c) => {
    const profile = await requireOwner(c);
    if (!profile) return c.redirect(u('/login'), 303);
    setAvatarRequested(env.db, profile.id, env.now());
    return c.redirect(u('/me'), 303);
  });

  app.post('/me/subscribers', async (c) => {
    const profile = await requireOwner(c);
    if (!profile) return c.redirect(u('/login'), 303);
    const form = await c.req.parseBody();
    const email = emailSchema.safeParse(form.email);
    if (email.success) {
      const name = String(form.name ?? '').trim().slice(0, 60) || null;
      const existing = findSubscriberByEmail(env.db, profile.id, email.data);
      if (existing) {
        if (existing.status !== 'approved') {
          approveSubscriber(env.db, existing.id, env.now());
          sendWelcome(env, findSubscriberById(env.db, existing.id)!);
        }
      } else {
        sendWelcome(env, createSubscriber(env.db, { profileId: profile.id, email: email.data, name, status: 'approved', addedBy: 'owner', now: env.now() }));
      }
    }
    return c.redirect(u('/me'), 303);
  });

  app.post('/me/subscribers/:id/:action{approve|block}', async (c) => {
    const profile = await requireOwner(c);
    if (!profile) return c.redirect(u('/login'), 303);
    const sub = findSubscriberById(env.db, Number(c.req.param('id')));
    if (sub && sub.profile_id === profile.id) {
      if (c.req.param('action') === 'approve') {
        if (sub.status !== 'approved') {
          approveSubscriber(env.db, sub.id, env.now());
          sendWelcome(env, findSubscriberById(env.db, sub.id)!);
        }
      } else {
        blockSubscriber(env.db, sub.id);
      }
    }
    return c.redirect(u('/me'), 303);
  });

  // --- dev mailbox ----------------------------------------------------

  if (dev) {
    app.get('/dev/mailbox', (c) => {
      const rows = listOutbox(env.db);
      return c.html(
        <Simple title="Dev mailbox" heading="Dev mailbox">
          {rows.length === 0 ? <p>Nothing sent yet.</p> : null}
          <ul class="list mail">
            {rows.map((m) => (
              <li>
                <span>
                  <a href={u(`/dev/mailbox/${m.id}`)}>{m.subject}</a>
                  <br />
                  <span class="tag">
                    to {m.to_email} · {m.status} · {formatFull(m.created_at, env.config.displayTz)}
                  </span>
                </span>
              </li>
            ))}
          </ul>
        </Simple>,
      );
    });

    app.get('/dev/mailbox/:id', (c) => {
      const m = findOutbox(env.db, Number(c.req.param('id')));
      if (!m) return c.notFound();
      // Links in the plain-text body become clickable; cid:photo in the
      // HTML is pointed at the PNG route below so the preview shows it.
      const linked = m.text_body
        .split(/(https?:\/\/\S+)/)
        .map((part, i) => (i % 2 ? <a href={part}>{part}</a> : part));
      const html = m.html_body.replace('cid:photo', `/dev/mailbox/${m.id}/photo.png`);
      return c.html(
        <Simple title={m.subject} heading={m.subject}>
          <div class="mail">
            <p>
              To {m.to_email} · {m.status} · <a href={u('/dev/mailbox')}>back</a>
            </p>
            <pre>{linked}</pre>
            <iframe srcdoc={html} title="HTML version"></iframe>
          </div>
        </Simple>,
      );
    });

    app.get('/dev/mailbox/:id/photo.png', (c) => {
      const m = findOutbox(env.db, Number(c.req.param('id')));
      const photo = m?.photo_id ? findPhotoById(env.db, m.photo_id) : null;
      if (!photo) return c.notFound();
      const pbm = parsePbm(new Uint8Array(readFileSync(join(env.config.dataDir, photo.path))));
      return c.body(pbmToPng(pbm), 200, { 'content-type': 'image/png' });
    });
  }

  return app;
}
