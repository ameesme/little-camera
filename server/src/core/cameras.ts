import { shortCode } from '@little-camera/pbm';
import type { Env } from '../env.js';
import { cameraLinkedMail } from '../emails/index.js';
import { queueEmail } from '../lib/email.js';
import { hashSecret, secretMatches } from '../middleware/cameraAuth.js';
import { appUrl, blogUrl } from '../config.js';
import { confirmAddressMail, deviceClaimMail } from '../emails/index.js';
import { createToken, verifyToken } from '../lib/tokens.js';
import {
  approveCameraClaim,
  bindCamera,
  createCamera,
  findCamera,
  setCameraFirmware,
  setCameraVerifiedEmail,
  setCameraTelemetry,
  touchCamera,
  type CameraRow,
  type Telemetry,
} from '../repo/cameras.js';
import { findBlogById, type BlogRow } from '../repo/blogs.js';
import { deleteCodesForBlog, findLiveCode } from '../repo/codes.js';

export interface BoundInfo {
  handle: string;
  name: string;
  url: string;
}

/** The `bound` object of every camera API response, or null for a camera no blog has claimed. */
export function boundInfo(env: Env, camera: CameraRow): BoundInfo | null {
  const blog = camera.blog_id !== null ? findBlogById(env.db, camera.blog_id) : null;
  if (!blog) return null;
  return { handle: blog.handle, name: blog.name, url: blogUrl(env.config, blog.handle) };
}

export interface HelloInput {
  id: string;
  secret: string;
  firmware?: string;
}

/** Trust on first use: the first secret seen for an id is the one kept, so a later different one is an impostor. */
export function sayHello(env: Env, input: HelloInput): CameraRow | null {
  const now = env.now();
  const known = findCamera(env.db, input.id);
  if (known && !secretMatches(known, input.secret)) return null;

  let camera =
    known ??
    createCamera(env.db, {
      id: input.id,
      secretHash: hashSecret(input.secret),
      shortCode: shortCode(input.id),
      now,
    });
  if (known) touchCamera(env.db, camera.id, now);
  if (input.firmware && input.firmware !== camera.firmware_version) {
    setCameraFirmware(env.db, camera.id, input.firmware, now);
    camera = findCamera(env.db, camera.id)!;
  }
  return camera;
}

export function bind(env: Env, camera: CameraRow, blogId: number): CameraRow {
  bindCamera(env.db, camera.id, blogId, env.now());
  const blog = findBlogById(env.db, blogId);
  if (blog) {
    queueEmail(env, blog.email, cameraLinkedMail({ name: blog.name, blogUrl: blogUrl(env.config, blog.handle) }));
  }
  return findCamera(env.db, camera.id)!;
}

/** A verification code read out of an uploaded photo. Null when no live code matches, or when it is the blog the camera already posts to. */
export function bindByCode(env: Env, camera: CameraRow, code: string): CameraRow | null {
  const live = findLiveCode(env.db, code, env.now());
  if (!live || live.blog_id === camera.blog_id) return null;
  const bound = bind(env, camera, live.blog_id);
  deleteCodesForBlog(env.db, live.blog_id);
  return bound;
}

export function relayTelemetry(env: Env, camera: CameraRow, t: Telemetry): CameraRow {
  setCameraTelemetry(env.db, camera.id, t, env.now());
  return findCamera(env.db, camera.id)!;
}

export type ClaimState = 'unbound' | 'pending' | 'approved';

/** Approved only for the blog the owner actually allowed, so a pre-approval cannot follow the camera elsewhere. */
export function claimState(camera: CameraRow): ClaimState {
  if (camera.blog_id === null) return 'unbound';
  return camera.claim_blog_id === camera.blog_id ? 'approved' : 'pending';
}

export function letPhoneIn(env: Env, camera: CameraRow, blogId: number): CameraRow {
  approveCameraClaim(env.db, camera.id, blogId, env.now());
  return findCamera(env.db, camera.id)!;
}

/** Mails the blog's owner, asking whether this phone may manage it. */
export function requestClaim(env: Env, camera: CameraRow, blog: BlogRow): void {
  const { token } = createToken(env, { kind: 'device_claim', blogId: blog.id, cameraId: camera.id });
  queueEmail(
    env,
    blog.email,
    deviceClaimMail({
      name: blog.name,
      blogName: blogUrl(env.config, blog.handle).replace(/^https?:\/\//, ''),
      url: appUrl(env.config, `/claim?t=${token}`),
    }),
  );
}

/** Mails an address nobody has registered, asking its owner to confirm it before naming a blog (§4.9). */
export function requestAddressConfirmation(env: Env, camera: CameraRow, email: string): void {
  const { token } = createToken(env, { kind: 'address_verify', email, cameraId: camera.id });
  queueEmail(env, email, confirmAddressMail({ url: appUrl(env.config, `/confirm?t=${token}`) }));
}

/** Redeem that link. The address lands on the camera, which is the only thing allowed to name a blog for it. */
export function confirmAddress(env: Env, token: string | undefined): CameraRow | null {
  const verified = verifyToken(env, token, 'address_verify');
  if (!verified.ok || !verified.row.camera_id || !verified.row.email) return null;
  const camera = findCamera(env.db, verified.row.camera_id);
  if (!camera || camera.blog_id !== null) return null;
  setCameraVerifiedEmail(env.db, camera.id, verified.row.email);
  return findCamera(env.db, camera.id);
}

export interface Claim {
  camera: CameraRow;
  blog: BlogRow;
}

/** Redeem the emailed link. It names the camera, so it cannot be replayed onto another phone. */
export function approveClaim(env: Env, token: string | undefined): Claim | null {
  const verified = verifyToken(env, token, 'device_claim');
  if (!verified.ok || !verified.row.camera_id || verified.row.blog_id === null) return null;
  const camera = findCamera(env.db, verified.row.camera_id);
  const blog = findBlogById(env.db, verified.row.blog_id);
  if (!camera || !blog) return null;
  return { camera: letPhoneIn(env, camera, blog.id), blog };
}
