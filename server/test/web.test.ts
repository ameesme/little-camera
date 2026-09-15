import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { HANDLE, RESERVED_HANDLES, handleFromHost } from '../src/lib/handles.js';
import { findCamera } from '../src/repo/cameras.js';
import { listOutbox } from '../src/repo/outbox.js';
import { findProfileByHandle } from '../src/repo/profiles.js';
import { CAM, apex, cookieJar, fixture, hello, makeWorld, upload, type TestWorld } from './helpers.js';

let w: TestWorld;
beforeEach(() => {
  w = makeWorld();
});
afterEach(() => w.cleanup());

const form = (path: string, fields: Record<string, string>, cookie = '') =>
  w.app.request(
    apex(path, {
      method: 'POST',
      headers: { 'content-type': 'application/x-www-form-urlencoded', cookie },
      body: new URLSearchParams(fields),
    }),
  );

const linkFrom = (text: string, path: string) => text.match(new RegExp(`http://localhost:3000${path}\\?t=([A-Za-z0-9_-]+)`))![1];

/** Register, click the verification link, return the owner cookie. */
async function registerAndVerify(handle = 'mees', email = 'mees@example.com') {
  const res = await form('/register', { name: 'Mees', email, handle });
  expect(res.status).toBe(200);
  const mail = listOutbox(w.env.db)[0];
  const token = linkFrom(mail.text_body, '/verify-email');
  const verify = await w.app.request(apex(`/verify-email?t=${token}`));
  expect(verify.status).toBe(303);
  expect(verify.headers.get('location')).toBe('/me');
  const cookie = cookieJar(verify);
  expect(cookie).toMatch(/^owner=/);
  return cookie;
}

describe('handles', () => {
  it.each(['mees', 'a1', 'my-camera', 'x'.repeat(31)])('accepts %s', (h) => expect(HANDLE.test(h)).toBe(true));
  it.each(['', 'a', '-abc', 'Mees', 'has space', 'x'.repeat(32), 'ünïcode'])('rejects %j', (h) => expect(HANDLE.test(h)).toBe(false));
  it('reserves the apex names', () => {
    for (const r of ['www', 'api', 'dev', 'mail', 'admin', 'app']) expect(RESERVED_HANDLES.has(r)).toBe(true);
  });
  it('extracts the handle from a host', () => {
    expect(handleFromHost('mees.localhost:3000', 'localhost:3000')).toBe('mees');
    expect(handleFromHost('localhost:3000', 'localhost:3000')).toBeNull();
    expect(handleFromHost('a.b.localhost:3000', 'localhost:3000')).toBeNull();
    expect(handleFromHost('evil.com', 'localhost:3000')).toBeNull();
    expect(handleFromHost('mees.example.com', 'example.com')).toBe('mees');
  });
});

describe('registration', () => {
  it('validates the form', async () => {
    expect((await form('/register', { name: 'Mees', email: 'nope', handle: 'mees' })).status).toBe(400);
    expect((await form('/register', { name: '', email: 'a@b.co', handle: 'mees' })).status).toBe(400);
    expect((await form('/register', { name: 'Mees', email: 'a@b.co', handle: 'www' })).status).toBe(400);
    expect((await form('/register', { name: 'Mees', email: 'a@b.co', handle: 'Bad Handle' })).status).toBe(400);
    expect(await (await form('/register', { name: 'Mees', email: 'a@b.co', handle: 'www' })).text()).toContain('reserved');
  });

  it('registers, verifies by email link and lands on /me with the verification picture', async () => {
    const cookie = await registerAndVerify();
    const profile = findProfileByHandle(w.env.db, 'mees')!;
    expect(profile.email_verified_at).toBe(w.clock.t);

    const me = await w.app.request(apex('/me', { headers: { cookie } }));
    expect(me.status).toBe(200);
    const html = await me.text();
    expect(html).toContain('<svg');
    expect(html).toContain('This is my little camera');
    expect(html).toContain('Take a picture of this and press send');
    expect(html).toMatch(/LC:[A-Z2-9]{6}/);
    expect(html).toContain('action="/me/link-camera"');
    expect(html).toContain("fetch('/me/status'");
    expect(html).toContain('<meta name="color-scheme" content="light"');

    // The code is in the database and lives 15 minutes.
    const code = html.match(/LC:([A-Z2-9]{6})/)![1];
    const row = w.env.db.prepare('SELECT * FROM verification_codes WHERE code = ?').get(code) as { expires_at: number };
    expect(row.expires_at).toBe(w.clock.t + 900);

    const status = await w.app.request(apex('/me/status', { headers: { cookie } }));
    expect(await status.json()).toEqual({ bound: false, url: null });

    // Blog is reachable (teaser) once verified.
    expect((await w.app.request(new Request('http://mees.localhost:3000/', { headers: { host: 'mees.localhost:3000' } }))).status).toBe(200);
  });

  it('refuses a taken handle or email, and the blog stays 404 until verified', async () => {
    await form('/register', { name: 'Mees', email: 'mees@example.com', handle: 'mees' });
    expect((await w.app.request(new Request('http://mees.localhost:3000/', { headers: { host: 'mees.localhost:3000' } }))).status).toBe(404);
    expect((await form('/register', { name: 'X', email: 'other@example.com', handle: 'MEES' })).status).toBe(409);
    expect((await form('/register', { name: 'X', email: 'Mees@Example.com', handle: 'other' })).status).toBe(409);
  });

  it('expired verification links are refused', async () => {
    await form('/register', { name: 'Mees', email: 'mees@example.com', handle: 'mees' });
    const token = linkFrom(listOutbox(w.env.db)[0].text_body, '/verify-email');
    w.clock.t += 25 * 3600;
    expect((await w.app.request(apex(`/verify-email?t=${token}`))).status).toBe(410);
    expect((await w.app.request(apex(`/verify-email?t=garbage`))).status).toBe(410);
  });

  it('/me redirects to /login without a cookie', async () => {
    const res = await w.app.request(apex('/me'));
    expect(res.status).toBe(303);
    expect(res.headers.get('location')).toBe('/login');
  });
});

describe('login', () => {
  it('mails a one-hour link to known owners and says the same thing to strangers', async () => {
    await registerAndVerify();
    const a = await form('/login', { email: 'mees@example.com' });
    const b = await form('/login', { email: 'nobody@example.com' });
    expect(await a.text()).toBe(await b.text());
    const mails = listOutbox(w.env.db);
    expect(mails).toHaveLength(2); // verify + login
    const token = linkFrom(mails[0].text_body, '/login');
    w.clock.t += 61 * 60;
    expect((await w.app.request(apex(`/login?t=${token}`))).status).toBe(410);
    w.clock.t -= 61 * 60;
    const ok = await w.app.request(apex(`/login?t=${token}`));
    expect(ok.status).toBe(303);
    expect(cookieJar(ok)).toMatch(/^owner=/);
  });
});

describe('/me link-camera fallback', () => {
  it('binds by short code only when the camera uploaded within 10 minutes', async () => {
    const cookie = await registerAndVerify();
    await hello(w);
    // No upload yet → refused.
    let res = await form('/me/link-camera', { short_code: 'MM48F3' }, cookie);
    expect(res.status).toBe(400);
    expect(await res.text()).toContain('10 minutes');

    await upload(w, fixture('cat'), 1);
    w.clock.t += 11 * 60;
    res = await form('/me/link-camera', { short_code: 'mm48f3' }, cookie);
    expect(res.status).toBe(400);

    await upload(w, fixture('cup'), 2);
    res = await form('/me/link-camera', { short_code: ' mm48f3 ' }, cookie);
    expect(res.status).toBe(303);
    expect(findCamera(w.env.db, CAM.id)!.profile_id).toBe(1);
    // Earlier photos now belong to the profile.
    const n = w.env.db.prepare('SELECT COUNT(*) AS n FROM photos WHERE profile_id = 1').get() as { n: number };
    expect(n.n).toBe(2);

    const status = await w.app.request(apex('/me/status', { headers: { cookie } }));
    expect(await status.json()).toEqual({ bound: true, url: 'http://mees.localhost:3000' });
    const me = await (await w.app.request(apex('/me', { headers: { cookie } }))).text();
    expect(me).toContain('http://mees.localhost:3000');
    expect(me).toContain('Request profile picture');
  });

  it('rate limits to 10 tries per hour', async () => {
    const cookie = await registerAndVerify();
    for (let i = 0; i < 10; i++) {
      expect(await (await form('/me/link-camera', { short_code: 'AAAAAA' }, cookie)).text()).toContain('No camera');
    }
    expect(await (await form('/me/link-camera', { short_code: 'AAAAAA' }, cookie)).text()).toContain('Too many');
    w.clock.t += 3601;
    expect(await (await form('/me/link-camera', { short_code: 'AAAAAA' }, cookie)).text()).toContain('No camera');
  });
});

describe('owner subscriber management on /me', () => {
  it('adds, approves and blocks', async () => {
    const cookie = await registerAndVerify();
    await hello(w);
    await upload(w, fixture('cat'), 1);
    await form('/me/link-camera', { short_code: 'MM48F3' }, cookie);
    expect((await form('/me/subscribers', { email: 'sanne@example.com', name: 'Sanne' }, cookie)).status).toBe(303);
    const subs = w.env.db.prepare('SELECT * FROM subscribers').all() as { id: number; status: string; name: string }[];
    expect(subs).toHaveLength(1);
    expect(subs[0]).toMatchObject({ status: 'approved', name: 'Sanne' });
    expect(listOutbox(w.env.db)[0].to_email).toBe('sanne@example.com');
    await form(`/me/subscribers/${subs[0].id}/block`, {}, cookie);
    expect((w.env.db.prepare('SELECT status FROM subscribers').get() as { status: string }).status).toBe('blocked');
    await form(`/me/subscribers/${subs[0].id}/approve`, {}, cookie);
    expect((w.env.db.prepare('SELECT status FROM subscribers').get() as { status: string }).status).toBe('approved');
    const me = await (await w.app.request(apex('/me', { headers: { cookie } }))).text();
    expect(me).toContain('sanne@example.com');
    await form('/me/request-avatar', {}, cookie);
    expect(await (await w.app.request(apex('/me', { headers: { cookie } }))).text()).toContain('next picture you take');
  });
});

describe('dev mailbox', () => {
  it('lists and shows mails outside production', async () => {
    await form('/register', { name: 'Mees', email: 'mees@example.com', handle: 'mees' });
    const list = await (await w.app.request(apex('/dev/mailbox'))).text();
    expect(list).toContain('Confirm your email');
    const one = await (await w.app.request(apex('/dev/mailbox/1'))).text();
    expect(one).toContain('/verify-email?t=');
    expect(one).toContain('srcdoc=');
  });
});
