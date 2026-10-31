import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { approveCameraClaim, bindCamera, findCamera } from '../src/repo/cameras.js';
import { findPhotoByPublicId } from '../src/repo/photos.js';
import { listOutbox } from '../src/repo/outbox.js';
import { letPhoneIn } from '../src/core/cameras.js';
import { findBlogByHandle } from '../src/repo/blogs.js';
import { commentsForPhotos, insertComment } from '../src/repo/comments.js';
import { createSubscriber, listSubscribers } from '../src/repo/subscribers.js';
import { apex, appPage, blog as blogPage, body, CAM, cameraDelete, cameraGet, cameraPost, confirmAddress, fixture, hello, makeBlog, makeWorld, upload, type TestWorld } from './helpers.js';

let w: TestWorld;
beforeEach(() => {
  w = makeWorld();
});
afterEach(() => w.cleanup());

describe('POST /api/camera/hello', () => {
  it('creates the camera on first contact and derives the short code', async () => {
    const r = await hello(w);
    expect(r.status).toBe(200);
    expect(r.body).toEqual({ camera_id: CAM.id, short_code: 'MM48F3', bound: null, server_time: w.clock.t });
    const again = await hello(w);
    expect(again.status).toBe(200);
    expect(again.body.short_code).toBe('MM48F3');
  });

  it('rejects a different secret for a known camera', async () => {
    await hello(w);
    const r = await hello(w, { id: CAM.id, secret: 'ffffffffffffffffffffffffffffffff' });
    expect(r.status).toBe(401);
    expect(r.body.error).toBe('unauthorized');
  });

  it('validates the body', async () => {
    const r = await hello(w, { id: 'nope', secret: 'x' });
    expect(r.status).toBe(400);
  });
});

describe('camera auth', () => {
  it('401 without or with a wrong Authorization header', async () => {
    await hello(w);
    const none = await w.app.request(apex('/api/camera/status'));
    expect(none.status).toBe(401);
    const wrong = await w.app.request(
      apex('/api/camera/status', { headers: { authorization: `Camera ${CAM.id}:00000000000000000000000000000000` } }),
    );
    expect(wrong.status).toBe(401);
    const unknown = await w.app.request(
      apex('/api/camera/status', { headers: { authorization: `Camera 000000000000:${CAM.secret}` } }),
    );
    expect(unknown.status).toBe(401);
  });
});

describe('POST /api/camera/photos', () => {
  beforeEach(async () => {
    await hello(w);
  });

  it('stores a photo, writes the file, then reports a duplicate', async () => {
    const bytes = fixture('cat');
    const r = await upload(w, bytes, 1);
    expect(r.status).toBe(201);
    expect(r.body).toMatchObject({ status: 'stored', kind: 'photo', bound: null });
    expect(r.body.id).toMatch(/^[a-z0-9]{12}$/);
    const row = findPhotoByPublicId(w.env.db, r.body.id)!;
    expect(existsSync(join(w.env.config.dataDir, row.path))).toBe(true);
    expect(row.path).toBe(join('photos', CAM.id, `${row.sha256}.pbm`));

    const dup = await upload(w, bytes, 1);
    expect(dup.status).toBe(200);
    expect(dup.body).toMatchObject({ id: r.body.id, status: 'duplicate', kind: 'photo' });
  });

  it('dates from the camera clock when the file has t > 0', async () => {
    const r = await upload(w, fixture('cup', { boot: 3, up: 1000, t: 1_700_000_000 }), 2, {
      'x-captured-at': '1234',
      'x-captured-at-source': 'phone',
    });
    const row = findPhotoByPublicId(w.env.db, r.body.id)!;
    expect(row.captured_at).toBe(1_700_000_000);
    expect(row.captured_at_source).toBe('camera');
  });

  it("dates from the phone's estimate when t = 0", async () => {
    const r = await upload(w, fixture('cup', { boot: 3, up: 1000, t: 0 }), 2, {
      'x-captured-at': '1700000500',
      'x-captured-at-source': 'phone',
    });
    const row = findPhotoByPublicId(w.env.db, r.body.id)!;
    expect(row).toMatchObject({ captured_at: 1_700_000_500, captured_at_source: 'phone' });
  });

  it('dates from the upload time otherwise and keeps a batch in index order', async () => {
    const a = await upload(w, fixture('cat'), 5);
    const b = await upload(w, fixture('cup'), 6);
    const c = await upload(w, fixture('moon'), 7, { 'x-captured-at': '0', 'x-captured-at-source': 'upload' });
    const rows = [a, b, c].map((r) => findPhotoByPublicId(w.env.db, r.body.id)!);
    expect(rows.map((r) => r.captured_at_source)).toEqual(['upload', 'upload', 'upload']);
    expect(rows.map((r) => r.captured_at)).toEqual([w.clock.t - 2, w.clock.t - 1, w.clock.t]);
    expect(rows.every((r) => r.uploaded_at === w.clock.t)).toBe(true);
  });

  it('rejects wrong content type, oversize bodies, non-P4 and wrong dimensions', async () => {
    const bad = await w.app.request(
      apex('/api/camera/photos', {
        method: 'POST',
        headers: { authorization: `Camera ${CAM.id}:${CAM.secret}`, 'content-type': 'image/png', 'x-photo-index': '1' },
        body: body(fixture('cat')),
      }),
    );
    expect(bad.status).toBe(415);
    const big = await upload(w, new Uint8Array(17_000), 1);
    expect(big.status).toBe(413);
    const p1 = await upload(w, new TextEncoder().encode('P1\n2 2\n0 1\n1 0\n'), 1);
    expect(p1.status).toBe(400);
    expect(p1.body.error).toBe('bad_pbm');
    const small = new Uint8Array([...new TextEncoder().encode('P4\n8 1\n'), 0xff]);
    const dims = await upload(w, small, 1);
    expect(dims.status).toBe(400);
    expect(dims.body.message).toMatch(/320/);
    const noIndex = await w.app.request(
      apex('/api/camera/photos', {
        method: 'POST',
        headers: { authorization: `Camera ${CAM.id}:${CAM.secret}`, 'content-type': 'image/x-portable-bitmap' },
        body: body(fixture('cat')),
      }),
    );
    expect(noIndex.status).toBe(400);
  });

  it('attaches photos to the blog once bound and turns the next one into the avatar', async () => {
    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    const before = await upload(w, fixture('cat', { boot: 1, up: 1, t: w.clock.t - 10 }), 1);
    expect(before.body.bound).toEqual({ handle: 'mees', name: 'Mees', url: 'http://mees.localhost:3000' });
    expect(before.body.kind).toBe('photo');

    const req = await cameraPost(w, '/request-avatar', {});
    expect(req.status).toBe(200);
    expect(req.body).toEqual({ requested_at: w.clock.t });

    // Taken before the request: stays a photo.
    const old = await upload(w, fixture('cup', { boot: 1, up: 1, t: w.clock.t - 5 }), 2);
    expect(old.body.kind).toBe('photo');
    // Taken after: becomes the avatar and clears the request.
    w.clock.t += 60;
    const av = await upload(w, fixture('moon', { boot: 1, up: 1, t: w.clock.t - 1 }), 3);
    expect(av.body.kind).toBe('avatar');
    const status = await cameraGet(w, '/status');
    expect(status.body.avatar).toEqual({ requested_at: null, has_avatar: true });
    expect(status.body.bound.photo_count).toBe(3);
  });
});

describe('GET /api/camera/status', () => {
  it('has the documented shape before and after binding', async () => {
    await hello(w);
    const unbound = await cameraGet(w, '/status');
    expect(unbound.status).toBe(200);
    expect(unbound.body).toEqual({
      camera_id: CAM.id,
      short_code: 'MM48F3',
      bound: null,
      avatar: { requested_at: null, has_avatar: false },
      firmware: { installed: null, latest: null, update_available: false },
      claim: 'unbound',
      pending: null,
      confirmed_email: null,
      subscribers: [],
      server_time: w.clock.t,
    });

    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    await cameraPost(w, '/subscribers', { email: 'Sanne@Example.com', name: 'Sanne' });
    const bound = await cameraGet(w, '/status');
    expect(bound.body.bound).toEqual({
      handle: 'mees',
      name: 'Mees',
      url: 'http://mees.localhost:3000',
      photo_count: 0,
    });
    // Bound, but the owner has not let this phone in yet.
    expect(bound.body.claim).toBe('pending');
    expect(bound.body.subscribers).toEqual([
      { id: 1, email: 'sanne@example.com', name: 'Sanne', status: 'approved', added_by: 'owner' },
    ]);
  });
});

describe('subscribers via the camera API', () => {
  it('refuses on an unbound camera', async () => {
    await hello(w);
    const r = await cameraPost(w, '/subscribers', { email: 'a@b.c' });
    expect(r.status).toBe(409);
  });

  it('owner-added subscribers are approved and welcomed; approve/block work by id', async () => {
    await hello(w);
    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    const add = await cameraPost(w, '/subscribers', { email: 'joris@example.com', name: null });
    expect(add.status).toBe(201);
    expect(add.body).toEqual({ id: 1 });
    const mails = listOutbox(w.env.db);
    expect(mails).toHaveLength(1);
    expect(mails[0].to_email).toBe('joris@example.com');
    expect(mails[0].text_body).toMatch(/http:\/\/mees\.localhost:3000\/\?t=/);

    const again = await cameraPost(w, '/subscribers', { email: 'joris@example.com' });
    expect(again.status).toBe(200);
    expect(again.body).toEqual({ id: 1 });

    expect((await cameraPost(w, '/subscribers/1/block', {})).status).toBe(200);
    expect((await cameraGet(w, '/status')).body.subscribers[0].status).toBe('blocked');
    expect((await cameraPost(w, '/subscribers/1/approve', {})).status).toBe(200);
    expect((await cameraGet(w, '/status')).body.subscribers[0].status).toBe('approved');
    expect((await cameraPost(w, '/subscribers/99/approve', {})).status).toBe(404);
  });

  it('does not let one camera touch another blog\'s subscribers', async () => {
    await hello(w);
    const other = { id: 'a1b2c3d4e5f6', secret: '11112222333344445555666677778888' };
    await hello(w, other);
    const p1 = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    const p2 = makeBlog(w.env, { handle: 'tess', name: 'Tess', email: 'tess@example.com' });
    bindCamera(w.env.db, CAM.id, p1.id, w.clock.t);
    bindCamera(w.env.db, other.id, p2.id, w.clock.t);
    const add = await cameraPost(w, '/subscribers', { email: 'x@example.com' });
    const r = await cameraPost(w, `/subscribers/${add.body.id}/block`, {}, other);
    expect(r.status).toBe(404);
    expect(findCamera(w.env.db, other.id)!.blog_id).toBe(p2.id);
  });
});

describe('GET /api/camera/handle', () => {
  it('answers while the owner types: taken, reserved and malformed are all unavailable', async () => {
    await hello(w);
    makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    expect((await cameraGet(w, '/handle?handle=mees')).body).toEqual({ handle: 'mees', available: false });
    expect((await cameraGet(w, '/handle?handle=MEES')).body).toEqual({ handle: 'mees', available: false });
    expect((await cameraGet(w, '/handle?handle=www')).body.available).toBe(false);
    expect((await cameraGet(w, '/handle?handle=a')).body.available).toBe(false);
    expect((await cameraGet(w, '/handle?handle=-nope')).body.available).toBe(false);
    expect((await cameraGet(w, '/handle?handle=sanne')).body).toEqual({ handle: 'sanne', available: true });
  });
});

describe('POST /api/camera/telemetry', () => {
  const reading = {
    battery_percent: 44,
    battery_mv: 3820,
    charging: false,
    used_bytes: 1234567,
    total_bytes: 6553600,
    happiness: 200,
    mood_elapsed_s: 90061,
  };

  it('stores what the phone read out of Info, and the blog shows that number', async () => {
    await hello(w);
    expect((await cameraPost(w, '/telemetry', reading)).status).toBe(200);
    const camera = findCamera(w.env.db, CAM.id)!;
    expect(camera.battery_percent).toBe(44);
    expect(camera.battery_mv).toBe(3820);
    expect(camera.charging).toBe(0);
    expect(camera.happiness).toBe(200);
    expect(camera.telemetry_at).toBe(w.clock.t);

    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    await upload(w, fixture('cat'), 1);
    const html = await (await w.app.request(blogPage('mees', '/'))).text();
    expect(html).toContain('44%');
  });

  it('shows no battery at all until a phone has reported one', async () => {
    await hello(w);
    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    await upload(w, fixture('cat'), 1);
    const html = await (await w.app.request(blogPage('mees', '/'))).text();
    expect(html).not.toContain('class="batt"');
  });

  it('refuses a percentage outside 0-100', async () => {
    await hello(w);
    expect((await cameraPost(w, '/telemetry', { ...reading, battery_percent: 101 })).status).toBe(400);
  });
});

describe('the phone making a blog of its own', () => {
  it('names it for the confirmed address and lets this phone in', async () => {
    await hello(w);
    // There is nothing to name a blog for until someone has opened the link.
    const early = await cameraPost(w, '/blog', { name: 'Mees', handle: 'mees' });
    expect(early.status).toBe(409);
    expect(early.body.error).toBe('address_unconfirmed');

    confirmAddress(w, 'mees@example.com');
    expect((await cameraGet(w, '/status')).body.confirmed_email).toBe('mees@example.com');

    const r = await cameraPost(w, '/blog', { name: 'Mees', handle: 'mees' });
    expect(r.status).toBe(201);
    expect(r.body).toEqual({ handle: 'mees', url: 'http://mees.localhost:3000' });

    // Live from birth, on the address that was confirmed, and no second link to open.
    const made = findBlogByHandle(w.env.db, 'mees')!;
    expect(made.email).toBe('mees@example.com');
    expect(made.email_verified_at).toBe(w.clock.t);
    // ponytail: temporary — the app skips the §5 code photo, so naming binds. Expect blog_id null again
    // once the card comes back, and `pending` to cover the gap between naming and binding.
    expect(findCamera(w.env.db, CAM.id)!.blog_id).toBe(made.id);

    const status = await cameraGet(w, '/status');
    expect(status.body.claim).toBe('approved');
    // Spent: the address belongs to the blog now, so it cannot name a second one.
    expect(status.body.confirmed_email).toBeNull();
    expect(status.body.pending).toBeNull();
    expect(status.body.bound.handle).toBe('mees');
  });

  it('refuses a taken handle, a reserved one, and a camera that already has a blog', async () => {
    await hello(w);
    makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    confirmAddress(w, 'other@example.com');
    expect((await cameraPost(w, '/blog', { name: 'M', handle: 'mees' })).status).toBe(409);
    expect((await cameraPost(w, '/blog', { name: 'M', handle: 'Reserved!' })).status).toBe(400);

    bindCamera(w.env.db, CAM.id, 1, w.clock.t);
    expect((await cameraPost(w, '/blog', { name: 'M', handle: 'fresh' })).status).toBe(409);
  });

  // Two cameras can confirm the same address before either names a blog; the second one loses.
  it('refuses an address that grew a blog while this phone was still typing', async () => {
    await hello(w);
    confirmAddress(w, 'mees@example.com');
    makeBlog(w.env, { handle: 'first', name: 'First', email: 'mees@example.com' });
    const r = await cameraPost(w, '/blog', { name: 'M', handle: 'second' });
    expect(r.status).toBe(409);
    expect(r.body.error).toBe('email_taken');
  });
});

describe('letting a phone in', () => {
  const claimLink = () => {
    const mail = listOutbox(w.env.db).find((m) => /new phone/i.test(m.subject))!;
    return mail.text_body.match(/\/app\/claim\?t=([A-Za-z0-9_-]+)/)![1];
  };

  it('a phone holding a bound camera asks the owner, who has to confirm on the web', async () => {
    await hello(w);
    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    expect((await cameraGet(w, '/status')).body.claim).toBe('pending');
    // Until the owner says yes, the phone cannot read the blog.
    expect((await cameraGet(w, '/feed')).status).toBe(403);

    expect((await cameraPost(w, '/claim', {})).body).toEqual({ state: 'pending' });
    const token = claimLink();

    // The GET only asks; a mail scanner prefetching it grants nothing.
    const page = await w.app.request(appPage(`/claim?t=${token}`));
    expect(page.status).toBe(200);
    expect(await page.text()).toContain('MM48F3');
    expect((await cameraGet(w, '/status')).body.claim).toBe('pending');

    const done = await w.app.request(
      appPage('/claim', {
        method: 'POST',
        headers: { 'content-type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ t: token }),
      }),
    );
    expect(done.status).toBe(200);
    expect((await cameraGet(w, '/status')).body.claim).toBe('approved');
    expect((await cameraGet(w, '/feed')).status).toBe(200);
  });

  it('an unbound camera claims by email, and the same answer comes back for an address nobody has', async () => {
    await hello(w);
    makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    expect((await cameraPost(w, '/claim', { email: 'nobody@example.com' })).body).toEqual({ state: 'pending' });
    // An address nobody owns is offered a blog of its own, never a way into someone else's.
    expect(listOutbox(w.env.db).some((m) => /new phone/i.test(m.subject))).toBe(false);
    expect(listOutbox(w.env.db).map((m) => m.subject)).toEqual(['Confirm your email for your little camera']);
    expect(findCamera(w.env.db, CAM.id)!.verified_email).toBeNull();

    expect((await cameraPost(w, '/claim', { email: 'mees@example.com' })).body).toEqual({ state: 'pending' });
    await w.app.request(
      appPage('/claim', {
        method: 'POST',
        headers: { 'content-type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ t: claimLink() }),
      }),
    );
    // Approved for that blog ahead of binding; the QR is still what binds it.
    expect(findCamera(w.env.db, CAM.id)!.claim_blog_id).toBe(1);
    expect(findCamera(w.env.db, CAM.id)!.blog_id).toBeNull();
  });

  it('a claim approved for one blog does not follow the camera to another', async () => {
    await hello(w);
    const mees = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    const tess = makeBlog(w.env, { handle: 'tess', name: 'Tess', email: 'tess@example.com' });
    bindCamera(w.env.db, CAM.id, mees.id, w.clock.t);
    await cameraPost(w, '/claim', {});
    await w.app.request(
      appPage('/claim', {
        method: 'POST',
        headers: { 'content-type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ t: claimLink() }),
      }),
    );
    expect((await cameraGet(w, '/status')).body.claim).toBe('approved');

    bindCamera(w.env.db, CAM.id, tess.id, w.clock.t);
    expect((await cameraGet(w, '/status')).body.claim).toBe('pending');
    expect((await cameraGet(w, '/feed')).status).toBe(403);
  });

  it('an expired link grants nothing', async () => {
    await hello(w);
    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    await cameraPost(w, '/claim', {});
    const token = claimLink();
    w.clock.t += 3601;
    expect((await w.app.request(appPage(`/claim?t=${token}`))).status).toBe(410);
    expect((await cameraGet(w, '/status')).body.claim).toBe('pending');
  });
});

describe('GET /api/camera/feed', () => {
  async function approvedWorld() {
    await hello(w);
    const blog = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, blog.id, w.clock.t);
    letPhoneIn(w.env, findCamera(w.env.db, CAM.id)!, blog.id);
    return blog;
  }

  it('returns the pictures newest first, with their comments and a PNG url', async () => {
    const blog = await approvedWorld();
    const first = await upload(w, fixture('cat'), 1);
    w.clock.t += 60;
    const second = await upload(w, fixture('cup'), 2);
    insertComment(w.env.db, { photoId: 1, subscriberId: null, body: 'Mine', now: w.clock.t });

    const r = await cameraGet(w, '/feed');
    expect(r.status).toBe(200);
    expect(r.body.photos.map((p: { id: string }) => p.id)).toEqual([second.body.id, first.body.id]);
    expect(r.body.photos[1]).toMatchObject({
      url: `http://mees.localhost:3000/photos/${first.body.id}.png`,
      kind: 'photo',
      comments: [{ name: blog.name, body: 'Mine' }],
    });
    expect(r.body.photos[0].comments).toEqual([]);
    expect(r.body.next).toBeNull();
  });

  it('refuses on an unbound camera', async () => {
    await hello(w);
    expect((await cameraGet(w, '/feed')).status).toBe(409);
  });

  it('the owner comments under the blog name, with a timestamp the feed carries', async () => {
    const blog = await approvedWorld();
    const cat = await upload(w, fixture('cat'), 1);
    w.clock.t += 120;

    expect((await cameraPost(w, `/feed/${cat.body.id}/comments`, { body: '   ' })).status).toBe(400);
    const posted = await cameraPost(w, `/feed/${cat.body.id}/comments`, { body: 'it is the cat' });
    expect(posted.status).toBe(201);
    expect(posted.body).toEqual({ name: blog.name });

    const r = await cameraGet(w, '/feed');
    expect(r.body.photos[0].comments).toEqual([
      { id: 1, name: blog.name, body: 'it is the cat', created_at: w.clock.t },
    ]);

    expect((await cameraPost(w, '/feed/nosuchphoto12/comments', { body: 'hi' })).status).toBe(404);
  });

  it('deletes one picture with its comments and its file, and only off its own blog', async () => {
    await approvedWorld();
    const cat = await upload(w, fixture('cat'), 1);
    w.clock.t += 60;
    const cup = await upload(w, fixture('cup'), 2);
    insertComment(w.env.db, { photoId: 1, subscriberId: null, body: 'Mine', now: w.clock.t });
    const path = join(w.env.config.dataDir, findPhotoByPublicId(w.env.db, cat.body.id)!.path);
    expect(existsSync(path)).toBe(true);

    expect((await cameraDelete(w, `/feed/${cat.body.id}`)).body).toEqual({ ok: true });
    expect(existsSync(path)).toBe(false);
    expect(findPhotoByPublicId(w.env.db, cat.body.id)).toBeNull();
    expect(commentsForPhotos(w.env.db, [1], "Mees").size).toBe(0);

    const left = await cameraGet(w, '/feed');
    expect(left.body.photos.map((p: { id: string }) => p.id)).toEqual([cup.body.id]);

    expect((await cameraDelete(w, '/feed/nosuchphoto12')).status).toBe(404);
    expect((await cameraDelete(w, `/feed/${cat.body.id}`)).status).toBe(404);
  });
});

describe('DELETE /api/camera/subscribers/:id', () => {
  it('removes one for good, and will not reach onto another blog list', async () => {
    await hello(w);
    const mees = makeBlog(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, mees.id, w.clock.t);
    await cameraPost(w, '/subscribers', { email: 'sanne@example.com', name: 'Sanne' });

    const tess = makeBlog(w.env, { handle: 'tess', name: 'Tess', email: 'tess@example.com' });
    const theirs = createSubscriber(w.env.db, {
      blogId: tess.id,
      email: 'other@example.com',
      name: null,
      status: 'approved',
      addedBy: 'owner',
      now: w.clock.t,
    });
    expect((await cameraDelete(w, `/subscribers/${theirs.id}`)).status).toBe(404);

    expect((await cameraDelete(w, '/subscribers/1')).status).toBe(200);
    expect(listSubscribers(w.env.db, mees.id)).toHaveLength(0);
    expect((await cameraDelete(w, '/subscribers/1')).status).toBe(404);
  });
});

describe('POST /api/camera/verification-code', () => {
  it('hands the phone a code once it has a blog, and refuses once the camera is bound', async () => {
    await hello(w);
    expect((await cameraPost(w, '/verification-code', {})).status).toBe(409);

    // A code is for moving an already-bound camera to another blog (§5), so it needs a claim on one
    // the camera is not posting to. Naming a blog binds it for now, which is the 409 at the end.
    const other = makeBlog(w.env, { handle: 'other', name: 'Other', email: 'other@example.com' });
    approveCameraClaim(w.env.db, CAM.id, other.id, w.clock.t);
    const made = await cameraPost(w, '/verification-code', {});
    expect(made.status).toBe(201);
    expect(made.body.code).toMatch(/^[A-Z2-9]{6}$/);
    expect(made.body.expires_at - w.clock.t).toBe(15 * 60);

    bindCamera(w.env.db, CAM.id, other.id, w.clock.t);
    expect((await cameraPost(w, '/verification-code', {})).status).toBe(409);
  });
});
