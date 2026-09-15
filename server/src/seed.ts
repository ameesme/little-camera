// `pnpm seed`: a local demo blog that matches the mockup. Creates the
// profile `mees`, a bound camera, the eight fixture photos with the
// mockup's dates and comments, four approved subscribers, and prints a
// 48-hour link for sanne@example.com so the blog can be opened right away.
//
// Photos go through the real ingest path (encoded with a `t=` comment so
// they are dated from the "camera clock"), which also exercises the code
// a real upload would run.

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodePbm, parsePbm, shortCode } from '@little-camera/pbm';
import { blogUrl, loadConfig } from './config.js';
import { now, openDb } from './db.js';
import type { Env } from './env.js';
import { ingestPhoto } from './lib/ingest.js';
import { createToken } from './lib/tokens.js';
import { hashSecret } from './middleware/cameraAuth.js';
import { bindCamera, createCamera, findCamera } from './repo/cameras.js';
import { insertComment } from './repo/comments.js';
import { setPhotoKind } from './repo/photos.js';
import { createProfile, findProfileByHandle, markEmailVerified, setAvatarPhoto } from './repo/profiles.js';
import { createSubscriber, type SubscriberRow } from './repo/subscribers.js';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'packages', 'pbm', 'fixtures');

/** Local time in Europe/Amsterdam during summer (CEST = UTC+2), as unix seconds. */
const cest = (y: number, m: number, d: number, h: number, min: number) => Math.floor(Date.UTC(y, m - 1, d, h - 2, min) / 1000);

// Newest first, as on the mockup page. The "four/six/seven" fixtures are
// the mockup's un-captioned posts at those positions.
const POSTS: { file: string; at: number; comments: [string, string][] }[] = [
  { file: 'cup', at: cest(2026, 8, 8, 8, 12), comments: [['Sanne', 'third one today?'], ['Joris', 'the light in this one']] },
  { file: 'cat', at: cest(2026, 8, 7, 19, 34), comments: [['Fenna', 'she knows'], ['Tess', 'best subscriber'], ['Joris', '♥']] },
  { file: 'photographer', at: cest(2026, 8, 6, 15, 2), comments: [['Tess', 'a camera taking a picture of a camera']] },
  { file: 'four', at: cest(2026, 8, 6, 9, 18), comments: [] },
  { file: 'moon', at: cest(2026, 8, 5, 23, 51), comments: [['Sanne', 'how did it even get this'], ['Fenna', '1 bit moon']] },
  { file: 'six', at: cest(2026, 8, 4, 13, 7), comments: [['Joris', 'did you buy them']] },
  { file: 'seven', at: cest(2026, 8, 3, 18, 40), comments: [] },
  { file: 'texture', at: cest(2026, 8, 2, 7, 55), comments: [['Tess', 'texture guy']] },
];

const CAMERA = { id: '7cdfa1e2b3c4', secret: '9f2c0000111122223333444455556666' };

export function seed(env: Env): { link: string } {
  if (findProfileByHandle(env.db, 'mees')) throw new Error('profile "mees" already exists; delete DATA_DIR to reseed');
  const t0 = POSTS[POSTS.length - 1].at - 3600; // registered an hour before the first picture

  const profile = createProfile(env.db, { handle: 'mees', name: 'Mees', email: 'mees@example.com', now: t0 });
  markEmailVerified(env.db, profile.id, t0);

  if (!findCamera(env.db, CAMERA.id)) {
    createCamera(env.db, { id: CAMERA.id, secretHash: hashSecret(CAMERA.secret), shortCode: shortCode(CAMERA.id), now: t0 });
  }
  bindCamera(env.db, CAMERA.id, profile.id, t0);
  const camera = findCamera(env.db, CAMERA.id)!;

  const subs = new Map<string, SubscriberRow>();
  for (const [name, email] of [
    ['Sanne', 'sanne@example.com'],
    ['Joris', 'joris@example.com'],
    ['Fenna', 'fenna@example.com'],
    ['Tess', 'tess@example.com'],
  ]) {
    subs.set(name, createSubscriber(env.db, { profileId: profile.id, email, name, status: 'approved', addedBy: 'owner', now: t0 }));
  }

  // Oldest first so ids and upload order agree with the dates.
  let avatarPhotoId: number | null = null;
  let newestPublicId = '';
  [...POSTS].reverse().forEach((post, i) => {
    const src = parsePbm(new Uint8Array(readFileSync(join(FIXTURES, `${post.file}.pbm`))));
    const bytes = encodePbm(src.width, src.height, src.rows, { boot: 1, up: 1000 * (i + 1), t: post.at });
    const { photo } = ingestPhoto(env, camera, bytes, { cameraIndex: i + 1, uploadedAt: post.at + 60 });
    for (const [name, body] of post.comments) {
      insertComment(env.db, { photoId: photo.id, subscriberId: subs.get(name)!.id, body, now: post.at + 600 });
    }
    if (post.file === 'photographer') avatarPhotoId = photo.id;
    newestPublicId = photo.public_id;
  });

  // The mockup's profile picture is an 88 px PNG; on a real blog the avatar
  // is a camera photo, so use the "camera taking a picture of a camera" one.
  if (avatarPhotoId !== null) {
    setPhotoKind(env.db, avatarPhotoId, 'avatar');
    setAvatarPhoto(env.db, profile.id, avatarPhotoId);
  }

  // Everyone has "been notified" up to now, so the newsletter job does not
  // mail the whole archive on first run.
  env.db.prepare('UPDATE subscribers SET last_notified_at = ? WHERE profile_id = ?').run(env.now(), profile.id);

  const sanne = subs.get('Sanne')!;
  const { token } = createToken(env, { kind: 'subscriber', profileId: profile.id, subscriberId: sanne.id });
  return { link: `${blogUrl(env.config, profile.handle)}/p/${newestPublicId}?t=${token}` };
}

const isMain = process.argv[1] && fileURLToPath(import.meta.url) === process.argv[1];
if (isMain) {
  const config = loadConfig();
  const env: Env = { db: openDb(join(config.dataDir, 'little-camera.sqlite')), config, now };
  const { link } = seed(env);
  console.log(`seeded ${blogUrl(config, 'mees')}`);
  console.log(`camera ${CAMERA.id} secret ${CAMERA.secret} (short code ${shortCode(CAMERA.id)})`);
  console.log(`owner sign-in: POST /login with mees@example.com, then open /dev/mailbox`);
  console.log(`subscriber link for sanne@example.com (48 h):\n${link}`);
}
