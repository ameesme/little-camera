import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { parsePbm, type Pbm } from '@little-camera/pbm';
import type { Env } from '../env.js';
import { publicId } from '../lib/ids.js';
import { decodeVerificationCode } from '../lib/qr.js';
import { findCamera, type CameraRow } from '../repo/cameras.js';
import { blogUrl } from '../config.js';
import { commentsForPhotos, insertComment } from '../repo/comments.js';
import { anyLiveCode } from '../repo/codes.js';
import { findBlogById, setAvatarPhoto, type BlogRow } from '../repo/blogs.js';
import { setSubscriberName } from '../repo/subscribers.js';
import type { Viewer } from './blogs.js';
import {
  feedPage,
  findPhotoByHash,
  insertPhoto,
  setPhotoKind,
  shiftUploadBatchBack,
  type CapturedAtSource,
  type PhotoRow,
} from '../repo/photos.js';
import { bindByCode } from './cameras.js';

export const MAX_PHOTO_BYTES = 16 * 1024;
export const MAX_COMMENT = 1000;
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

// ponytail: every upload pays a QR decode while any blog has a live code, and a bound camera rebinds to whatever it photographs.
function tryVerification(env: Env, camera: CameraRow, pbm: Pbm, photo: PhotoRow): { camera: CameraRow; photo: PhotoRow } {
  if (!anyLiveCode(env.db, env.now())) return { camera, photo };
  const code = decodeVerificationCode(pbm);
  const bound = code ? bindByCode(env, camera, code) : null;
  if (!bound) return { camera, photo };
  setPhotoKind(env.db, photo.id, 'verification');
  return { camera: bound, photo: { ...photo, kind: 'verification', blog_id: bound.blog_id } };
}

/** The first picture taken after the owner asked for a profile picture becomes one. */
function tryAvatar(env: Env, camera: CameraRow, photo: PhotoRow, capturedAt: number): PhotoRow {
  if (camera.blog_id === null || photo.kind !== 'photo') return photo;
  const blog = findBlogById(env.db, camera.blog_id);
  if (!blog?.avatar_requested_at || capturedAt <= blog.avatar_requested_at) return photo;
  setPhotoKind(env.db, photo.id, 'avatar');
  setAvatarPhoto(env.db, blog.id, photo.id);
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
    blog_id: camera.blog_id,
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

export interface FeedComment {
  id: number;
  name: string;
  body: string;
}

export interface FeedPhoto {
  id: string;
  url: string;
  kind: string;
  captured_at: number;
  uploaded_at: number;
  comments: FeedComment[];
}

/** The owner's own feed: what the blog shows, as data, newest first. */
export function feed(env: Env, blog: BlogRow, limit: number, before?: { uploadedAt: number; id: number }): FeedPhoto[] {
  const photos = feedPage(env.db, blog.id, limit, before);
  const comments = commentsForPhotos(env.db, photos.map((p) => p.id), blog.name);
  const base = blogUrl(env.config, blog.handle);
  return photos.map((p) => ({
    id: p.public_id,
    url: `${base}/photos/${p.public_id}.png`,
    kind: p.kind,
    captured_at: p.captured_at,
    uploaded_at: p.uploaded_at,
    comments: (comments.get(p.id) ?? []).map((cm) => ({ id: cm.id, name: cm.name, body: cm.body })),
  }));
}

export type Commented = { ok: true; name: string } | { ok: false; reason: 'unauthorized' | 'bad_body' | 'name_required' };

/** The owner comments under the blog's name; a subscriber gives theirs on the first comment and never again. */
export function comment(env: Env, blog: BlogRow, photo: PhotoRow, viewer: Viewer, input: { body: string; name: string }): Commented {
  if (viewer.kind === 'anonymous') return { ok: false, reason: 'unauthorized' };
  if (!input.body || input.body.length > MAX_COMMENT) return { ok: false, reason: 'bad_body' };

  let name = blog.name;
  if (viewer.kind === 'subscriber') {
    if (!viewer.name && !input.name) return { ok: false, reason: 'name_required' };
    name = viewer.name ?? input.name;
    if (!viewer.name) setSubscriberName(env.db, viewer.id, name);
  }
  insertComment(env.db, {
    photoId: photo.id,
    subscriberId: viewer.kind === 'subscriber' ? viewer.id : null,
    body: input.body,
    now: env.now(),
  });
  return { ok: true, name };
}
