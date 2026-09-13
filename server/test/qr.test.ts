import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import QRCode from 'qrcode';
import { encodePbm, parsePbm } from '@little-camera/pbm';
import { fakeCameraShot } from '../src/lib/simulate.js';
import { decodeVerificationCode, verificationQrSvg } from '../src/lib/qr.js';
import { insertCode } from '../src/repo/codes.js';
import { findCamera } from '../src/repo/cameras.js';
import { findPhotoByPublicId } from '../src/repo/photos.js';
import { listOutbox } from '../src/repo/outbox.js';
import { CAM, cameraGet, fixture, hello, makeProfile, makeWorld, upload, type TestWorld } from './helpers.js';

let w: TestWorld;
beforeEach(async () => {
  w = makeWorld();
  await hello(w);
});
afterEach(() => w.cleanup());

const shotBytes = (code: string, modulePx: number, blur = 1) => {
  const pbm = fakeCameraShot(`LC:${code}`, { modulePx, blur, offsetX: 11, offsetY: -6 });
  return encodePbm(pbm.width, pbm.height, pbm.rows, { boot: 2, up: 5000, t: 0 });
};

describe('verification QR', () => {
  it('fits in a version 1 symbol at ECC H and renders as SVG', async () => {
    expect(QRCode.create('LC:KQ7M2X', { errorCorrectionLevel: 'H' }).version).toBe(1);
    const svg = await verificationQrSvg('KQ7M2X');
    expect(svg).toMatch(/^<svg/);
    expect(svg).toContain('#000000');
    expect(svg).not.toMatch(/#(?!000000|ffffff)[0-9a-f]{6}/i);
  });

  it.each([7, 8, 10])('decodes a dithered shot at %i px per module', (modulePx) => {
    const pbm = fakeCameraShot('LC:ABCDEF', { modulePx, blur: 2 });
    expect(decodeVerificationCode(pbm)).toBe('ABCDEF');
  });

  it('returns null for an ordinary photo', () => {
    expect(decodeVerificationCode(parsePbm(fixture('cat')))).toBeNull();
  });

  it.each([7, 8, 10])('binds the camera through the real ingest path (%i px modules)', async (modulePx) => {
    const profile = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    // An earlier photo from before the owner registered: must be back-filled.
    const earlier = await upload(w, fixture('cat'), 1);
    expect(earlier.body.bound).toBeNull();

    const code = insertCode(w.env.db, 'KQ7M2X', profile.id, w.clock.t);
    expect(code.expires_at - code.created_at).toBe(15 * 60);

    const r = await upload(w, shotBytes('KQ7M2X', modulePx), 2);
    expect(r.status).toBe(201);
    expect(r.body.kind).toBe('verification');
    expect(r.body.bound).toEqual({ handle: 'mees', name: 'Mees', url: 'http://mees.localhost:3000' });

    const camera = findCamera(w.env.db, CAM.id)!;
    expect(camera.profile_id).toBe(profile.id);
    expect(camera.bound_at).toBe(w.clock.t);
    expect(findPhotoByPublicId(w.env.db, earlier.body.id)!.profile_id).toBe(profile.id);
    expect(findPhotoByPublicId(w.env.db, r.body.id)!.kind).toBe('verification');

    const mails = listOutbox(w.env.db);
    expect(mails).toHaveLength(1);
    expect(mails[0].to_email).toBe('mees@example.com');
    expect(mails[0].subject).toMatch(/linked/i);

    // The verification shot is not a post.
    const status = await cameraGet(w, '/status');
    expect(status.body.bound.photo_count).toBe(1);
  });

  it('ignores a QR when the code is expired or unknown', async () => {
    const profile = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    insertCode(w.env.db, 'KQ7M2X', profile.id, w.clock.t);
    const wrong = await upload(w, shotBytes('ZZZZZZ', 8), 1);
    expect(wrong.body.kind).toBe('photo');
    expect(wrong.body.bound).toBeNull();

    w.clock.t += 16 * 60;
    const late = await upload(w, shotBytes('KQ7M2X', 8), 2);
    expect(late.body.kind).toBe('photo');
    expect(findCamera(w.env.db, CAM.id)!.profile_id).toBeNull();
  });

  it('does not rebind an already bound camera', async () => {
    const p1 = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    const p2 = makeProfile(w.env, { handle: 'tess', name: 'Tess', email: 'tess@example.com' });
    insertCode(w.env.db, 'AAAAAA', p1.id, w.clock.t);
    await upload(w, shotBytes('AAAAAA', 8), 1);
    insertCode(w.env.db, 'BBBBBB', p2.id, w.clock.t);
    const r = await upload(w, shotBytes('BBBBBB', 8), 2);
    expect(r.body.kind).toBe('photo');
    expect(findCamera(w.env.db, CAM.id)!.profile_id).toBe(p1.id);
  });
});
