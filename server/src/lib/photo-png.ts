import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { parsePbmFrames, pbmFramesToApng, type Pbm } from '@little-camera/pbm';
import type { Env } from '../env.js';

// Played back at the camera's own interval (protocol §2.1), but never so fast it flickers or so slow it reads as stills.
export const DEFAULT_FRAME_MS = 400;
const MIN_FRAME_MS = 100;
const MAX_FRAME_MS = 2000;

export const frameMsOf = (pbm: Pbm): number =>
  Math.min(MAX_FRAME_MS, Math.max(MIN_FRAME_MS, pbm.meta.ms ?? DEFAULT_FRAME_MS));

/**
 * A stored PBM as the PNG a browser shows: a plain one for a still, an APNG
 * that loops for a motion photo. The pixels are the file's, unscaled.
 */
export function photoPng(env: Env, photo: { path: string }): Uint8Array<ArrayBuffer> {
  const frames = parsePbmFrames(new Uint8Array(readFileSync(join(env.config.dataDir, photo.path))));
  return pbmFramesToApng(frames, frameMsOf(frames[0]));
}
