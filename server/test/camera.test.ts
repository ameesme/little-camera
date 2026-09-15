import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { bindCamera, findCamera } from '../src/repo/cameras.js';
import { findPhotoByPublicId } from '../src/repo/photos.js';
import { listOutbox } from '../src/repo/outbox.js';
import { CAM, apex, body, cameraGet, cameraPost, fixture, hello, makeProfile, makeWorld, upload, type TestWorld } from './helpers.js';

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

  it('attaches photos to the profile once bound and turns the next one into the avatar', async () => {
    const profile = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, profile.id, w.clock.t);
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
      subscribers: [],
      server_time: w.clock.t,
    });

    const profile = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, profile.id, w.clock.t);
    await cameraPost(w, '/subscribers', { email: 'Sanne@Example.com', name: 'Sanne' });
    const bound = await cameraGet(w, '/status');
    expect(bound.body.bound).toEqual({
      handle: 'mees',
      name: 'Mees',
      url: 'http://mees.localhost:3000',
      photo_count: 0,
      battery: expect.any(Number),
    });
    expect(bound.body.bound.battery).toBeGreaterThanOrEqual(97);
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
    const profile = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    bindCamera(w.env.db, CAM.id, profile.id, w.clock.t);
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
    const p1 = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    const p2 = makeProfile(w.env, { handle: 'tess', name: 'Tess', email: 'tess@example.com' });
    bindCamera(w.env.db, CAM.id, p1.id, w.clock.t);
    bindCamera(w.env.db, other.id, p2.id, w.clock.t);
    const add = await cameraPost(w, '/subscribers', { email: 'x@example.com' });
    const r = await cameraPost(w, `/subscribers/${add.body.id}/block`, {}, other);
    expect(r.status).toBe(404);
    expect(findCamera(w.env.db, other.id)!.profile_id).toBe(p2.id);
  });
});
