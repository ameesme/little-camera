// The photo pipeline behind POST /api/camera/photos (protocol §4.3), in
// the documented order: parse + hash → dedupe → date → QR check → avatar
// check. Kept out of the route so the seed script and tests can push
// photos through the exact same path.

import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { parsePbm, type Pbm } from '@little-camera/pbm';
import { blogUrl } from '../config.js';
import type { Env } from '../env.js';
import { cameraLinkedMail } from '../emails/index.js';
import { bindCamera, findCamera, type CameraRow } from '../repo/cameras.js';
import { anyLiveCode, deleteCodesForProfile, findLiveCode } from '../repo/codes.js';
import { findProfileById, setAvatarPhoto } from '../repo/profiles.js';
import { findPhotoByHash, insertPhoto, setPhotoKind, shiftUploadBatchBack, type CapturedAtSource, type PhotoRow } from '../repo/photos.js';
import { queueEmail } from './email.js';
import { publicId } from './ids.js';
import { decodeVerificationCode } from './qr.js';

export const MAX_PHOTO_BYTES = 16 * 1024;

export class IngestError extends Error {
  constructor(
    public status: 400 | 413 | 415,
    public code: string,
    message: string,
  ) {
    super(message);
  }
}

export interface IngestInput {
  /** `X-Photo-Index` */
  cameraIndex: number;
  /** `X-Captured-At`, the phone's estimate when the file has t=0. */
  capturedAtHint?: number;
  /** `X-Captured-At-Source`, `phone` or `upload`. */
  capturedAtSourceHint?: string;
  /** Override the upload time (the seed uses this to back-date its demo posts). */
  uploadedAt?: number;
}

export interface IngestResult {
  status: 'stored' | 'duplicate';
  photo: PhotoRow;
  /** The camera row after the upload; may have just become bound. */
  camera: CameraRow;
}

export function ingestPhoto(env: Env, camera: CameraRow, body: Uint8Array, input: IngestInput): IngestResult {
  if (body.length > MAX_PHOTO_BYTES) throw new IngestError(413, 'too_large', 'Photo larger than 16 KB');

  // 1. parse + hash. The hash covers the bytes as uploaded (header
  // included) because that is the file we store and serve unchanged.
  let pbm: Pbm;
  try {
    pbm = parsePbm(body);
  } catch (err) {
    throw new IngestError(400, 'bad_pbm', (err as Error).message);
  }
  if (pbm.width !== 320 || pbm.height !== 240) {
    throw new IngestError(400, 'bad_pbm', `Expected 320×240, got ${pbm.width}×${pbm.height}`);
  }
  const sha256 = createHash('sha256').update(body).digest('hex');

  // 2. dedupe per camera. The bridge retries uploads after a dropped
  // connection, so this is the normal path, not an error.
  const existing = findPhotoByHash(env.db, camera.id, sha256);
  if (existing) return { status: 'duplicate', photo: existing, camera: findCamera(env.db, camera.id) ?? camera };

  // 3. date (protocol §2)
  const now = input.uploadedAt ?? env.now();
  let capturedAt: number;
  let source: CapturedAtSource;
  if (pbm.meta.t && pbm.meta.t > 0) {
    capturedAt = pbm.meta.t;
    source = 'camera';
  } else if (input.capturedAtHint && input.capturedAtHint > 0 && input.capturedAtSourceHint !== 'upload') {
    capturedAt = input.capturedAtHint;
    source = 'phone';
  } else {
    shiftUploadBatchBack(env.db, camera.id, input.cameraIndex, now);
    capturedAt = now;
    source = 'upload';
  }

  // 4. store the file first, then the row: an orphan file is harmless, a
  // row pointing at nothing is a broken image on the blog.
  const relPath = join('photos', camera.id, `${sha256}.pbm`);
  const absPath = join(env.config.dataDir, relPath);
  mkdirSync(dirname(absPath), { recursive: true });
  writeFileSync(absPath, body);

  let photo = insertPhoto(env.db, {
    public_id: publicId(),
    camera_id: camera.id,
    profile_id: camera.profile_id,
    kind: 'photo',
    sha256,
    path: relPath,
    camera_index: input.cameraIndex,
    captured_at: capturedAt,
    captured_at_source: source,
    uploaded_at: now,
  });

  // 5. verification: only worth the CPU when someone is actually waiting
  // on /me with a live code.
  let current = camera;
  if (current.profile_id === null && anyLiveCode(env.db, env.now())) {
    const code = decodeVerificationCode(pbm);
    const live = code ? findLiveCode(env.db, code, env.now()) : null;
    if (live) {
      bindCamera(env.db, current.id, live.profile_id, env.now());
      setPhotoKind(env.db, photo.id, 'verification');
      deleteCodesForProfile(env.db, live.profile_id);
      const profile = findProfileById(env.db, live.profile_id);
      if (profile) {
        queueEmail(env, profile.email, cameraLinkedMail({ name: profile.name, blogUrl: blogUrl(env.config, profile.handle) }));
      }
      current = findCamera(env.db, current.id)!;
      photo = { ...photo, kind: 'verification', profile_id: live.profile_id };
    }
  }

  // 6. avatar: the first picture taken after the owner asked for one.
  if (current.profile_id !== null && photo.kind === 'photo') {
    const profile = findProfileById(env.db, current.profile_id);
    if (profile?.avatar_requested_at && capturedAt > profile.avatar_requested_at) {
      setPhotoKind(env.db, photo.id, 'avatar');
      setAvatarPhoto(env.db, profile.id, photo.id);
      photo = { ...photo, kind: 'avatar' };
    }
  }

  return { status: 'stored', photo, camera: current };
}
