import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { createToken } from '../src/lib/tokens.js';
import { bindCamera } from '../src/repo/cameras.js';
import { listOutbox } from '../src/repo/outbox.js';
import { createSubscriber, findSubscriberById, type SubscriberRow } from '../src/repo/subscribers.js';
import type { ProfileRow } from '../src/repo/profiles.js';
import { CAM, blog, cookieJar, fixture, hello, makeProfile, makeWorld, upload, type TestWorld } from './helpers.js';

let w: TestWorld;
let mees: ProfileRow;
let ids: string[];

beforeEach(async () => {
  w = makeWorld();
  mees = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
  await hello(w);
  bindCamera(w.env.db, CAM.id, mees.id, w.clock.t);
  ids = [];
  for (const [i, name] of ['texture', 'seven', 'six', 'moon'].entries()) {
    w.clock.t += 3600;
    const r = await upload(w, fixture(name, { boot: 1, up: 1, t: w.clock.t - 30 }), i + 1);
    ids.push(r.body.id);
  }
});
afterEach(() => w.cleanup());

function approvedSubscriber(email = 'sanne@example.com', name: string | null = 'Sanne'): SubscriberRow {
  return createSubscriber(w.env.db, { profileId: mees.id, email, name, status: 'approved', addedBy: 'owner', now: w.clock.t });
}

async function subscriberCookie(sub: SubscriberRow): Promise<string> {
  const { token } = createToken(w.env, { kind: 'subscriber', profileId: mees.id, subscriberId: sub.id });
  const res = await w.app.request(blog('mees', `/p/${ids[3]}?t=${token}`));
  expect(res.status).toBe(303);
  expect(res.headers.get('location')).toBe(`/p/${ids[3]}`);
  const setCookie = res.headers.getSetCookie()[0];
  expect(setCookie).toMatch(/^sub=/);
  expect(setCookie).toMatch(/Max-Age=172800/);
  expect(setCookie).toMatch(/HttpOnly/);
  return cookieJar(res);
}

describe('teaser vs full feed', () => {
  it('shows anonymous visitors the header, join form, newest photo and a count', async () => {
    const res = await w.app.request(blog('mees', '/'));
    expect(res.status).toBe(200);
    const html = await res.text();
    expect(html).toContain('<meta name="color-scheme" content="light"');
    expect(html).toContain('Mees’s little camera');
    expect(html).toContain('action="/subscribe"');
    expect(html).toContain(`/photos/${ids[3]}.png`); // newest
    expect(html).not.toContain(`/photos/${ids[2]}.png`);
    expect(html).toContain('3 more pictures for subscribers');
    expect(html).not.toContain('class="cmtform"');
    // No colour but ink and paper: every hex token on the page is pure black or pure white.
    for (const hex of html.match(/#[0-9a-fA-F]{3,6}\b/g) ?? []) expect(['#000', '#fff', '#000000', '#ffffff']).toContain(hex.toLowerCase());
    expect(html).toContain('<img class="shot" width="320" height="240"');
  });

  it('shows subscribers everything, newest first, with comment forms and paging', async () => {
    const cookie = await subscriberCookie(approvedSubscriber());
    const res = await w.app.request(blog('mees', '/', { headers: { cookie } }));
    const html = await res.text();
    for (const id of ids) expect(html).toContain(`id="p-${id}"`);
    expect(html.indexOf(ids[3])).toBeLessThan(html.indexOf(ids[0]));
    expect(html).toContain('class="cmtform"');
    expect(html).toContain('No more photos');
    expect(html).toContain('aria-pressed="true"');
    expect(html).toMatch(/<span>\w{3} \d{1,2} \w{3}<\/span><span>·<\/span><span>\d\d:\d\d<\/span>/);
  });

  it('pages with ?before and the load-more link', async () => {
    for (let i = 0; i < 9; i++) {
      w.clock.t += 60;
      // Reuse the same bytes with a different t so the hash differs.
      await upload(w, fixture('cat', { boot: 1, up: 1, t: w.clock.t }), 10 + i);
    }
    const cookie = await subscriberCookie(approvedSubscriber());
    const first = await (await w.app.request(blog('mees', '/', { headers: { cookie } }))).text();
    const m = first.match(/href="(\/\?before=\d+&amp;bid=\d+)"/);
    expect(m).not.toBeNull();
    expect((first.match(/<article/g) ?? []).length).toBe(10);
    const second = await (await w.app.request(blog('mees', m![1].replace('&amp;', '&'), { headers: { cookie } }))).text();
    expect((second.match(/<article/g) ?? []).length).toBe(3);
    expect(second).toContain('No more photos');
    expect(second).toContain(`id="p-${ids[0]}"`);
  });

  it('opens a linked photo first', async () => {
    const cookie = await subscriberCookie(approvedSubscriber());
    const res = await w.app.request(blog('mees', `/p/${ids[1]}`, { headers: { cookie } }));
    const html = await res.text();
    expect(html).not.toContain(`id="p-${ids[3]}"`);
    expect(html.indexOf(`id="p-${ids[1]}"`)).toBeLessThan(html.indexOf(`id="p-${ids[0]}"`));
  });

  it('serves photos as cacheable 1-bit PNGs and the avatar as a checker until set', async () => {
    const res = await w.app.request(blog('mees', `/photos/${ids[0]}.png`));
    expect(res.status).toBe(200);
    expect(res.headers.get('content-type')).toBe('image/png');
    expect(res.headers.get('cache-control')).toBe('public, max-age=31536000, immutable');
    const etag = res.headers.get('etag')!;
    expect(etag).toMatch(/^"[0-9a-f]{64}"$/);
    const again = await w.app.request(blog('mees', `/photos/${ids[0]}.png`, { headers: { 'if-none-match': etag } }));
    expect(again.status).toBe(304);
    const png = new Uint8Array(await res.arrayBuffer());
    expect(png[24]).toBe(1); // bit depth 1
    expect((await w.app.request(blog('mees', '/photos/zzzzzzzzzzzz.png'))).status).toBe(404);

    const avatar = await w.app.request(blog('mees', '/avatar.png'));
    expect(avatar.status).toBe(200);
    const bytes = new Uint8Array(await avatar.arrayBuffer());
    expect(new DataView(bytes.buffer).getUint32(16)).toBe(240);
  });

  it('404s unknown handles and reserved ones bounce to the apex', async () => {
    expect((await w.app.request(blog('nobody', '/'))).status).toBe(404);
    const www = await w.app.request(blog('www', '/'));
    expect(www.status).toBe(302);
    expect(www.headers.get('location')).toBe('http://localhost:3000');
  });
});

describe('links and tokens', () => {
  it('rejects an expired token with the fresh-link page, prefilled', async () => {
    const sub = approvedSubscriber();
    const { token } = createToken(w.env, { kind: 'subscriber', profileId: mees.id, subscriberId: sub.id });
    w.clock.t += 49 * 3600;
    const res = await w.app.request(blog('mees', `/p/${ids[3]}?t=${token}`));
    expect(res.status).toBe(410);
    const html = await res.text();
    expect(html).toContain('This link has expired');
    expect(html).toContain('value="sanne@example.com"');
    expect(html).toContain('action="/request-link"');
  });

  it('stays valid until expiry even after first use (mail prefetch)', async () => {
    const sub = approvedSubscriber();
    const { token } = createToken(w.env, { kind: 'subscriber', profileId: mees.id, subscriberId: sub.id });
    expect((await w.app.request(blog('mees', `/p/${ids[3]}?t=${token}`))).status).toBe(303);
    w.clock.t += 3600;
    expect((await w.app.request(blog('mees', `/p/${ids[3]}?t=${token}`))).status).toBe(303);
  });

  it('request-link mails only approved subscribers but always says the same thing', async () => {
    approvedSubscriber();
    createSubscriber(w.env.db, { profileId: mees.id, email: 'pending@example.com', name: null, status: 'pending', addedBy: 'self', now: w.clock.t });
    const ask = (email: string) =>
      w.app.request(
        blog('mees', '/request-link', {
          method: 'POST',
          headers: { 'content-type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({ email }),
        }),
      );
    const a = await ask('sanne@example.com');
    const b = await ask('pending@example.com');
    const c = await ask('stranger@example.com');
    expect([a.status, b.status, c.status]).toEqual([200, 200, 200]);
    expect(await a.text()).toBe(await c.text());
    const mails = listOutbox(w.env.db);
    expect(mails).toHaveLength(1);
    expect(mails[0].to_email).toBe('sanne@example.com');
    expect(mails[0].text_body).toMatch(new RegExp(`http://mees\\.localhost:3000/p/${ids[3]}\\?t=`));
  });

  it('a blocked subscriber\'s cookie stops working', async () => {
    const sub = approvedSubscriber();
    const cookie = await subscriberCookie(sub);
    w.env.db.prepare(`UPDATE subscribers SET status = 'blocked' WHERE id = ?`).run(sub.id);
    const html = await (await w.app.request(blog('mees', '/', { headers: { cookie } }))).text();
    expect(html).toContain('more pictures for subscribers');
  });
});

describe('subscribing', () => {
  const join = (email: string, json = false) =>
    w.app.request(
      blog('mees', '/subscribe', {
        method: 'POST',
        headers: json
          ? { 'content-type': 'application/json', accept: 'application/json' }
          : { 'content-type': 'application/x-www-form-urlencoded' },
        body: json ? JSON.stringify({ email }) : new URLSearchParams({ email }),
      }),
    );

  it('creates a pending subscriber and tells the owner; approval sends the welcome link', async () => {
    const res = await join('Tess@Example.com');
    expect(res.status).toBe(303);
    expect(res.headers.get('location')).toBe('/?joined=sent');
    const sub = findSubscriberById(w.env.db, 1)!;
    expect(sub).toMatchObject({ email: 'tess@example.com', status: 'pending', added_by: 'self', name: null });
    let mails = listOutbox(w.env.db);
    expect(mails).toHaveLength(1);
    expect(mails[0].to_email).toBe('mees@example.com');
    expect(mails[0].subject).toContain('tess@example.com');

    // Joining again while pending: no second mail, a different notice.
    const again = await join('tess@example.com', true);
    expect(await again.json()).toEqual({ state: 'pending' });
    expect(listOutbox(w.env.db)).toHaveLength(1);

    // The owner approves from the camera API.
    const approve = await w.app.request(
      new Request('http://localhost:3000/api/camera/subscribers/1/approve', {
        method: 'POST',
        headers: { host: 'localhost:3000', authorization: `Camera ${CAM.id}:${CAM.secret}` },
      }),
    );
    expect(approve.status).toBe(200);
    mails = listOutbox(w.env.db);
    expect(mails).toHaveLength(2);
    expect(mails[0].to_email).toBe('tess@example.com');
    const link = mails[0].text_body.match(/http:\/\/mees\.localhost:3000\/p\/([a-z0-9]{12})\?t=([A-Za-z0-9_-]+)/)!;
    expect(link[1]).toBe(ids[3]);

    // The welcome link works and the feed is full.
    const enter = await w.app.request(blog('mees', `/p/${link[1]}?t=${link[2]}`));
    expect(enter.status).toBe(303);
    const feed = await (await w.app.request(blog('mees', '/', { headers: { cookie: cookieJar(enter) } }))).text();
    expect(feed).toContain(`id="p-${ids[0]}"`);

    // Already approved: just say so.
    const already = await join('tess@example.com', true);
    expect(await already.json()).toEqual({ state: 'already' });
    const notice = await (await w.app.request(blog('mees', '/?joined=already'))).text();
    expect(notice).toContain('You’re already in');
  });

  it('rejects junk emails', async () => {
    const res = await join('not-an-email', true);
    expect(res.status).toBe(400);
  });
});

describe('comments', () => {
  const post = (cookie: string, id: string, fields: Record<string, string>, json = false) =>
    w.app.request(
      blog('mees', `/p/${id}/comments`, {
        method: 'POST',
        headers: {
          cookie,
          'content-type': 'application/x-www-form-urlencoded',
          ...(json ? { accept: 'application/json' } : {}),
        },
        body: new URLSearchParams(fields),
      }),
    );

  it('asks a nameless subscriber for a name first, then remembers it', async () => {
    const sub = approvedSubscriber('joris@example.com', null);
    const cookie = await subscriberCookie(sub);
    let html = await (await w.app.request(blog('mees', '/', { headers: { cookie } }))).text();
    expect(html).toContain('name="name" placeholder="Your name"');

    const noName = await post(cookie, ids[3], { body: 'hi' }, true);
    expect(noName.status).toBe(400);
    expect(await noName.json()).toEqual({ error: 'name_required' });

    const ok = await post(cookie, ids[3], { name: 'Joris', body: 'the light in this one' }, true);
    expect(ok.status).toBe(200);
    expect(await ok.json()).toEqual({ name: 'Joris', body: 'the light in this one' });
    expect(findSubscriberById(w.env.db, sub.id)!.name).toBe('Joris');

    html = await (await w.app.request(blog('mees', '/', { headers: { cookie } }))).text();
    expect(html).not.toContain('placeholder="Your name"');
    expect(html).toContain('<b>Joris</b><span>the light in this one</span>');

    const second = await post(cookie, ids[2], { body: 'did you buy them' });
    expect(second.status).toBe(303);
    expect(second.headers.get('location')).toBe(`/p/${ids[2]}`);
  });

  it('refuses anonymous comments and over-long ones', async () => {
    const anon = await post('', ids[3], { body: 'hi' }, true);
    expect(anon.status).toBe(401);
    const cookie = await subscriberCookie(approvedSubscriber());
    const long = await post(cookie, ids[3], { body: 'x'.repeat(1001) }, true);
    expect(long.status).toBe(400);
  });

  it('lets the owner comment under their own name', async () => {
    // The owner cookie is set on the apex; on localhost it has no Domain
    // so we forge the same signed cookie for the blog host in this test.
    const { setSignedCookie } = await import('hono/cookie');
    const { Hono } = await import('hono');
    const probe = new Hono();
    probe.get('/', async (c) => {
      await setSignedCookie(c, 'owner', String(mees.id), w.env.config.sessionSecret, { path: '/' });
      return c.text('ok');
    });
    const cookie = cookieJar(await probe.request('/'));
    const ok = await post(cookie, ids[3], { body: 'thanks all' }, true);
    expect(await ok.json()).toEqual({ name: 'Mees', body: 'thanks all' });
    const html = await (await w.app.request(blog('mees', '/', { headers: { cookie } }))).text();
    expect(html).toContain('<b>Mees</b><span>thanks all</span>');
    expect(html).toContain('href="http://localhost:3000/app/me"');
  });
});
