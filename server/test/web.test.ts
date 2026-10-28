import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { HANDLE, RESERVED_HANDLES, handleFromHost } from '../src/lib/handles.js';
import { listOutbox } from '../src/repo/outbox.js';
import { findBlogByHandle } from '../src/repo/blogs.js';
import { APEX, appPage, blog, cameraPost, hello, makeWorld, type TestWorld } from './helpers.js';

let w: TestWorld;
beforeEach(() => {
  w = makeWorld();
});
afterEach(() => w.cleanup());

const linkFrom = (text: string, path: string) =>
  text.match(new RegExp(`http://${APEX}/app${path}\\?t=([A-Za-z0-9_-]+)`))![1];

/** The app makes the blog; the web only confirms the address. */
async function makeBlogFromApp() {
  await hello(w);
  await cameraPost(w, '/blog', { name: 'Mees', email: 'mees@example.com', handle: 'mees' });
  return linkFrom(listOutbox(w.env.db)[0].text_body, '/verify-email');
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
  it('is what makes the blog answer, and hands the owner back to the app', async () => {
    const token = await makeBlogFromApp();
    expect((await w.app.request(blog('mees', '/'))).status).toBe(404);

    const res = await w.app.request(appPage(`/verify-email?t=${token}`));
    expect(res.status).toBe(200);
    const html = await res.text();
    expect(html).toContain('http://mees.localhost:3000');
    expect(html).toContain('Go back to the app');
    expect(findBlogByHandle(w.env.db, 'mees')!.email_verified_at).toBe(w.clock.t);
    expect((await w.app.request(blog('mees', '/'))).status).toBe(200);
  });

  it('refuses an expired or unknown link', async () => {
    const token = await makeBlogFromApp();
    w.clock.t += 25 * 3600;
    expect((await w.app.request(appPage(`/verify-email?t=${token}`))).status).toBe(410);
    expect((await w.app.request(appPage('/verify-email?t=garbage'))).status).toBe(410);
    expect((await w.app.request(appPage('/verify-email'))).status).toBe(410);
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
    await makeBlogFromApp();
    const list = await (await w.app.request(appPage('/dev/mailbox'))).text();
    expect(list).toContain('Confirm your email');
    const one = await (await w.app.request(appPage('/dev/mailbox/1'))).text();
    expect(one).toContain('/verify-email?t=');
    expect(one).toContain('srcdoc=');
  });
});
