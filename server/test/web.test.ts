import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { HANDLE, RESERVED_HANDLES, handleFromHost } from '../src/lib/handles.js';
import { listOutbox } from '../src/repo/outbox.js';
import { findBlogByHandle } from '../src/repo/blogs.js';
import { APEX, CAM, appPage, blog, cameraPost, hello, makeWorld, type TestWorld } from './helpers.js';
import { findCamera } from '../src/repo/cameras.js';

let w: TestWorld;
beforeEach(() => {
  w = makeWorld();
});
afterEach(() => w.cleanup());

const linkFrom = (text: string, path: string) =>
  text.match(new RegExp(`http://${APEX}/app${path}\\?t=([A-Za-z0-9_-]+)`))![1];

/** The app asks for a link to an address nobody owns yet; the web is where that link is opened. */
async function askToConfirm(email = 'mees@example.com') {
  await hello(w);
  await cameraPost(w, '/claim', { email });
  return linkFrom(listOutbox(w.env.db)[0].text_body, '/confirm');
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

describe('confirming the email', () => {
  it('is what lets a blog be named at all, and hands the owner back to the app', async () => {
    const token = await askToConfirm();
    // Nothing exists yet: the address has been asked about, not spent.
    expect(findCamera(w.env.db, CAM.id)!.verified_email).toBeNull();
    expect((await cameraPost(w, '/blog', { name: 'Mees', handle: 'mees' })).status).toBe(409);

    const res = await w.app.request(appPage(`/confirm?t=${token}`));
    expect(res.status).toBe(200);
    expect(await res.text()).toContain('Go back to the app');
    expect(findCamera(w.env.db, CAM.id)!.verified_email).toBe('mees@example.com');

    // Named now, and live from birth: no second link to open.
    expect((await cameraPost(w, '/blog', { name: 'Mees', handle: 'mees' })).status).toBe(201);
    expect(findBlogByHandle(w.env.db, 'mees')!.email_verified_at).toBe(w.clock.t);
    expect((await w.app.request(blog('mees', '/'))).status).toBe(200);
    // Spent once: the address belongs to the blog now, not to the camera.
    expect(findCamera(w.env.db, CAM.id)!.verified_email).toBeNull();
  });

  it('refuses an expired or unknown link', async () => {
    const token = await askToConfirm();
    w.clock.t += 25 * 3600;
    expect((await w.app.request(appPage(`/confirm?t=${token}`))).status).toBe(410);
    expect((await w.app.request(appPage('/confirm?t=garbage'))).status).toBe(410);
    expect((await w.app.request(appPage('/confirm'))).status).toBe(410);
  });

  // The whole point of doing the address first: an abandoned sign-up leaves a token, not a blog.
  it('writes no blog for an address nobody confirms', async () => {
    await askToConfirm();
    expect(findBlogByHandle(w.env.db, 'mees')).toBeNull();
    expect(w.env.db.prepare('SELECT count(*) AS n FROM blogs').get()).toEqual({ n: 0 });
  });
});

describe('the web manages nothing', () => {
  it('has no owner pages left to open', async () => {
    for (const path of ['/register', '/login', '/me', '/me/status', '/me/subscribers']) {
      expect((await w.app.request(appPage(path))).status).toBe(404);
    }
  });

  it('hands its own root back to the landing page', async () => {
    const res = await w.app.request(appPage(''));
    expect(res.status).toBe(302);
    expect(res.headers.get('location')).toBe(`http://${APEX}`);
  });
});

describe('dev mailbox', () => {
  it('lists and shows mails outside production', async () => {
    await askToConfirm();
    const list = await (await w.app.request(appPage('/dev/mailbox'))).text();
    expect(list).toContain('Confirm your email');
    const one = await (await w.app.request(appPage('/dev/mailbox/1'))).text();
    expect(one).toContain('/confirm?t=');
    expect(one).toContain('srcdoc=');
  });
});
