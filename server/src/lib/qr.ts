// Reading the verification QR out of a 1-bit camera photo (protocol §5).

import jsqrModule from 'jsqr';
import { majority3x3, scaleNearest, toRgba, type Pbm } from '@little-camera/pbm';

// jsqr is CommonJS: plain Node hands back `{ default: jsQR }` while tsx and vitest unwrap it, so accept either.
type JsQR = typeof import('jsqr').default;
const jsQR: JsQR =
  typeof jsqrModule === 'function' ? (jsqrModule as JsQR) : (jsqrModule as unknown as { default: JsQR }).default;

const PAYLOAD = /^LC:([A-Z2-9]{6})$/;

/** The six-character code inside the QR, or null when nothing decodes. */
export function decodeVerificationCode(pbm: Pbm): string | null {
  // A majority filter kills dither speckle and the upscales give jsQR's locator a few pixels per module; a clean shot hits on the first.
  const variants: Pbm[] = [pbm, majority3x3(pbm)];
  for (const base of variants) {
    for (const scale of [2, 3, 1]) {
      const img = scale === 1 ? base : scaleNearest(base, scale);
      const { data, width, height } = toRgba(img, 1);
      const result = jsQR(data, width, height, { inversionAttempts: 'attemptBoth' });
      const m = result?.data.trim().match(PAYLOAD);
      if (m) return m[1];
    }
  }
  return null;
}
