import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { parsePbm, type Pbm } from '@little-camera/pbm';
import type { Env } from '../env.js';
import { publicId } from '../lib/ids.js';
import { decodeVerificationCode } from '../lib/qr.js';
import { findCamera, type CameraRow } from '../repo/cameras.js';
import { anyLiveCode } from '../repo/codes.js';
import { findProfileById, setAvatarPhoto } from '../repo/profiles.js';
import {
  findPhotoByHash,
  insertPhoto,
  setPhotoKind,
  shiftUploadBatchBack,
  type CapturedAtSource,
  type PhotoRow,
} from '../repo/photos.js';
import { bindByCode } from './linking.js';

export const MAX_PHOTO_BYTES = 16 * 1024;
const WIDTH = 320;
const HEIGHT = 240;

export class PhotoRejected extends Error {
  constructor(
    public status: 400 | 413 | 415,
    public code: string,
    message: string,
  ) {
    super(message);
  }
}

export interface UploadInput {
  cameraIndex: number;
  capturedAtHint?: number;
  capturedAtSourceHint?: string;
  uploadedAt?: number;
}

export interface UploadResult {
  status: 'stored' | 'duplicate';
  photo: PhotoRow;
  camera: CameraRow;
}

function parse(body: Uint8Array): Pbm {
  if (body.length > MAX_PHOTO_BYTES) throw new PhotoRejected(413, 'too_large', 'Photo larger than 16 KB');
  let pbm: Pbm;
  try {
    pbm = parsePbm(body);
  } catch (err) {
    throw new PhotoRejected(400, 'bad_pbm', (err as Error).message);
  }
  if (pbm.width !== WIDTH || pbm.height !== HEIGHT) {
    throw new PhotoRejected(400, 'bad_pbm', `Expected ${WIDTH}×${HEIGHT}, got ${pbm.width}×${pbm.height}`);
  }
  return pbm;
}

/** Protocol §2: the camera's own clock, else the phone's estimate, else upload time. */
function dateOf(env: Env, camera: CameraRow, pbm: Pbm, input: UploadInput, uploadedAt: number): { capturedAt: number; source: CapturedAtSource } {
  if (pbm.meta.t && pbm.meta.t > 0) return { capturedAt: pbm.meta.t, source: 'camera' };
  if (input.capturedAtHint && input.capturedAtHint > 0 && input.capturedAtSourceHint !== 'upload') {
    return { capturedAt: input.capturedAtHint, source: 'phone' };
  }
  shiftUploadBatchBack(env.db, camera.id, input.cameraIndex, uploadedAt);
  return { capturedAt: uploadedAt, source: 'upload' };
}

/** The file lands before the row: an orphan file is harmless, a row pointing at nothing is a broken blog. */
function storeFile(env: Env, cameraId: string, sha256: string, body: Uint8Array): string {
  const relPath = join('photos', cameraId, `${sha256}.pbm`);
  const absPath = join(env.config.dataDir, relPath);
  mkdirSync(dirname(absPath), { recursive: true });
  writeFileSync(absPath, body);
  return relPath;
}

/** Binds the camera when the picture turns out to be of someone's verification code. */
function tryVerification(env: Env, camera: CameraRow, pbm: Pbm, photo: PhotoRow): { camera: CameraRow; photo: PhotoRow } {
  if (camera.profile_id !== null || !anyLiveCode(env.db, env.now())) return { camera, photo };
  const code = decodeVerificationCode(pbm);
  const bound = code ? bindByCode(env, camera, code) : null;
  if (!bound) return { camera, photo };
  setPhotoKind(env.db, photo.id, 'verification');
  return { camera: bound, photo: { ...photo, kind: 'verification', profile_id: bound.profile_id } };
}

/** The first picture taken after the owner asked for a profile picture becomes one. */
function tryAvatar(env: Env, camera: CameraRow, photo: PhotoRow, capturedAt: number): PhotoRow {
  if (camera.profile_id === null || photo.kind !== 'photo') return photo;
  const profile = findProfileById(env.db, camera.profile_id);
  if (!profile?.avatar_requested_at || capturedAt <= profile.avatar_requested_at) return photo;
  setPhotoKind(env.db, photo.id, 'avatar');
  setAvatarPhoto(env.db, profile.id, photo.id);
  return { ...photo, kind: 'avatar' };
}

export function uploadPhoto(env: Env, camera: CameraRow, body: Uint8Array, input: UploadInput): UploadResult {
  const pbm = parse(body);
  const sha256 = createHash('sha256').update(body).digest('hex');

  // The bridge re-uploads after a dropped connection, so a duplicate is the normal path rather than an error.
  const seen = findPhotoByHash(env.db, camera.id, sha256);
  if (seen) return { status: 'duplicate', photo: seen, camera: findCamera(env.db, camera.id) ?? camera };

  const uploadedAt = input.uploadedAt ?? env.now();
  const { capturedAt, source } = dateOf(env, camera, pbm, input, uploadedAt);

  const stored = insertPhoto(env.db, {
    public_id: publicId(),
    camera_id: camera.id,
    profile_id: camera.profile_id,
    kind: 'photo',
    sha256,
    path: storeFile(env, camera.id, sha256, body),
    camera_index: input.cameraIndex,
    captured_at: capturedAt,
    captured_at_source: source,
    uploaded_at: uploadedAt,
  });

  const verified = tryVerification(env, camera, pbm, stored);
  return { status: 'stored', photo: tryAvatar(env, verified.camera, verified.photo, capturedAt), camera: verified.camera };
}
