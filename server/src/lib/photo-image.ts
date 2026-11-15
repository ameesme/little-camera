import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { parsePbmFrames, pbmFramesToGif, pbmToPng, type Pbm } from '@little-camera/pbm';
import type { Env } from '../env.js';
import type { PhotoRow } from '../repo/photos.js';

// Played back at the camera's own interval (protocol §2.1), but never so fast it flickers or so slow it reads as stills.
export const DEFAULT_FRAME_MS = 400;
const MIN_FRAME_MS = 100;
const MAX_FRAME_MS = 2000;

export const frameMsOf = (pbm: Pbm): number =>
  Math.min(MAX_FRAME_MS, Math.max(MIN_FRAME_MS, pbm.meta.ms ?? DEFAULT_FRAME_MS));

export interface PhotoImage {
  bytes: Uint8Array<ArrayBuffer>;
  ext: 'png' | 'gif';
  type: 'image/png' | 'image/gif';
}

/** A still is served as a 1-bit PNG, a motion photo as a looping GIF: protocol §4.7. */
export const photoExt = (photo: Pick<PhotoRow, 'frames'>): PhotoImage['ext'] => (photo.frames > 1 ? 'gif' : 'png');

/**
 * The stored PBM in the form a browser shows. The pixels are the file's,
 * unscaled; only the container changes. The PBM on disk stays the photo.
 */
export function photoImage(env: Env, photo: Pick<PhotoRow, 'path' | 'frames'>): PhotoImage {
  const frames = parsePbmFrames(new Uint8Array(readFileSync(join(env.config.dataDir, photo.path))));
  if (photoExt(photo) === 'gif' && frames.length > 1) {
    return { bytes: pbmFramesToGif(frames, frameMsOf(frames[0])), ext: 'gif', type: 'image/gif' };
  }
  return { bytes: pbmToPng(frames[0]), ext: 'png', type: 'image/png' };
}
