import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { existsSync } from 'node:fs';
import { join } from 'node:path';
import { runNewsletter } from '../src/jobs/newsletter.js';
import { runPurge } from '../src/jobs/purge.js';
import { drainOutbox } from '../src/lib/email.js';
import { createToken } from '../src/lib/tokens.js';
import { bindCamera } from '../src/repo/cameras.js';
import { insertCode } from '../src/repo/codes.js';
import { listOutbox } from '../src/repo/outbox.js';
import { findPhotoByPublicId } from '../src/repo/photos.js';
import { createSubscriber, findSubscriberById } from '../src/repo/subscribers.js';
import { seed } from '../src/seed.js';
import { CAM, blog, fixture, hello, makeProfile, makeWorld, upload, type TestWorld } from './helpers.js';

let w: TestWorld;
beforeEach(async () => {
  w = makeWorld();
});
afterEach(() => w.cleanup());

describe('newsletter', () => {
  it('sends one email per subscriber per batch, deep-linking the newest photo', async () => {
    const mees = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    await hello(w);
    bindCamera(w.env.db, CAM.id, mees.id, w.clock.t);
    const sanne = createSubscriber(w.env.db, { profileId: mees.id, email: 'sanne@example.com', name: 'Sanne', status: 'approved', addedBy: 'owner', now: w.clock.t });
    createSubscriber(w.env.db, { profileId: mees.id, email: 'pending@example.com', name: null, status: 'pending', addedBy: 'self', now: w.clock.t });
    createSubscriber(w.env.db, { profileId: mees.id, email: 'blocked@example.com', name: null, status: 'blocked', addedBy: 'self', now: w.clock.t });

    expect(runNewsletter(w.env)).toBe(0);

    w.clock.t += 60;
    const a = await upload(w, fixture('cat'), 1);
    const b = await upload(w, fixture('cup'), 2);
    w.clock.t += 5;
    const c = await upload(w, fixture('moon'), 3);

    expect(runNewsletter(w.env)).toBe(1);
    const mails = listOutbox(w.env.db);
    expect(mails).toHaveLength(1);
    expect(mails[0].to_email).toBe('sanne@example.com');
    expect(mails[0].subject).toBe('Mees took 3 new pictures');
    expect(mails[0].photo_id).toBe(findPhotoByPublicId(w.env.db, c.body.id)!.id);
    expect(mails[0].html_body).toContain('cid:photo');
    const link = mails[0].text_body.match(/http:\/\/mees\.localhost:3000\/p\/([a-z0-9]{12})\?t=([A-Za-z0-9_-]+)/)!;
    expect(link[1]).toBe(c.body.id);
    expect(findSubscriberById(w.env.db, sanne.id)!.last_notified_at).toBe(w.clock.t);
    expect([a.status, b.status]).toEqual([201, 201]);

    // The link admits the subscriber for 48 hours.
    const enter = await w.app.request(blog('mees', `/p/${link[1]}?t=${link[2]}`));
    expect(enter.status).toBe(303);

    // Nothing new: nothing sent. One more photo: singular subject.
    expect(runNewsletter(w.env)).toBe(0);
    w.clock.t += 600;
    await upload(w, fixture('four'), 4);
    expect(runNewsletter(w.env)).toBe(1);
    expect(listOutbox(w.env.db)[0].subject).toBe('Mees took a picture');

    // A verification shot is not a post and does not trigger mail.
    w.clock.t += 600;
    w.env.db.prepare(`UPDATE photos SET kind = 'verification' WHERE public_id = ?`).run(a.body.id);
    expect(runNewsletter(w.env)).toBe(0);

    // The dev drainer marks everything sent without SMTP.
    expect(await drainOutbox(w.env)).toBe(2);
    expect(listOutbox(w.env.db).every((m) => m.status === 'sent')).toBe(true);
  });
});

describe('purge', () => {
  it('removes expired tokens and codes and week-old orphan photos with their files', async () => {
    const mees = makeProfile(w.env, { handle: 'mees', name: 'Mees', email: 'mees@example.com' });
    createToken(w.env, { kind: 'owner_login', profileId: mees.id });
    insertCode(w.env.db, 'ABCDEF', mees.id, w.clock.t);
    await hello(w);
    const orphan = await upload(w, fixture('cat'), 1);
    const path = join(w.env.config.dataDir, findPhotoByPublicId(w.env.db, orphan.body.id)!.path);

    expect(runPurge(w.env)).toEqual({ tokens: 0, codes: 0, photos: 0 });
    w.clock.t += 8 * 86400;
    expect(runPurge(w.env)).toEqual({ tokens: 1, codes: 1, photos: 1 });
    expect(existsSync(path)).toBe(false);
    expect(findPhotoByPublicId(w.env.db, orphan.body.id)).toBeNull();
  });
});

describe('seed', () => {
  it('builds the demo blog and its link works', async () => {
    const { link } = seed(w.env);
    const m = link.match(/^http:\/\/mees\.localhost:3000\/p\/([a-z0-9]{12})\?t=(.+)$/)!;
    expect(m).not.toBeNull();
    const enter = await w.app.request(blog('mees', `/p/${m[1]}?t=${m[2]}`));
    expect(enter.status).toBe(303);
    const cookie = enter.headers.getSetCookie()[0].split(';')[0];
    const html = await (await w.app.request(blog('mees', '/', { headers: { cookie } }))).text();
    expect((html.match(/<article/g) ?? []).length).toBe(8);
    expect(html).toContain('<span>Sat 8 Aug</span><span>·</span><span>08:12</span>');
    expect(html).toContain('<b>Tess</b><span>texture guy</span>');
    expect(html).toContain('<b>Joris</b><span>♥</span>');
    expect(html.indexOf('Sat 8 Aug')).toBeLessThan(html.indexOf('Sun 2 Aug'));
    const avatar = await w.app.request(blog('mees', '/avatar.png'));
    expect(avatar.headers.get('etag')).toMatch(/^"[0-9a-f]{64}"$/);
    expect(() => seed(w.env)).toThrow(/already exists/);
  });
});
