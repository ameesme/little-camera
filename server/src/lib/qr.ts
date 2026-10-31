// Reading the verification QR out of a 1-bit camera photo (protocol §5).

import jsqrModule from 'jsqr';
import { getPixel, majority3x3, scaleNearest, toRgba, type Pbm } from '@little-camera/pbm';

// jsqr is CommonJS: plain Node hands back `{ default: jsQR }` while tsx and vitest unwrap it, so accept either.
type JsQR = typeof import('jsqr').default;
const jsQR: JsQR =
  typeof jsqrModule === 'function' ? (jsqrModule as JsQR) : (jsqrModule as unknown as { default: JsQR }).default;

const PAYLOAD = /^([A-Z2-9]{6})$/;

interface Gray {
  data: Uint8ClampedArray;
  width: number;
  height: number;
}

// The camera dithers, so a white module photographed off a phone screen comes back as speckle at
// about the same spacing as the modules themselves, which jsQR's binarizer reads as structure.
// Averaging the window back into grey is what undoes that: the dither encoded the grey, and only
// the one-bit file threw it away. Nearest-upscaling afterwards gives the locator room to work.
function blurred(pbm: Pbm, radius: number, scale: number): Gray {
  const { width, height } = pbm;
  const gray = new Uint8ClampedArray(width * height);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let on = 0;
      let n = 0;
      for (let dy = -radius; dy <= radius; dy++) {
        const yy = y + dy;
        if (yy < 0 || yy >= height) continue;
        for (let dx = -radius; dx <= radius; dx++) {
          const xx = x + dx;
          if (xx < 0 || xx >= width) continue;
          if (getPixel(pbm, xx, yy)) on++;
          n++;
        }
      }
      gray[y * width + x] = 255 - Math.round((on / n) * 255);
    }
  }

  const w = width * scale;
  const h = height * scale;
  const data = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) {
    const sy = (y / scale) | 0;
    for (let x = 0; x < w; x++) {
      const v = gray[sy * width + ((x / scale) | 0)];
      const i = (y * w + x) * 4;
      data[i] = v;
      data[i + 1] = v;
      data[i + 2] = v;
      data[i + 3] = 255;
    }
  }
  return { data, width: w, height: h };
}

function read(img: Gray): string | null {
  const result = jsQR(img.data, img.width, img.height, { inversionAttempts: 'attemptBoth' });
  return result?.data.trim().match(PAYLOAD)?.[1] ?? null;
}

/** The six-character code inside the QR, or null when nothing decodes. */
export function decodeVerificationCode(pbm: Pbm): string | null {
  // A clean shot hits on the first of these; the rest are for a photo taken off a phone screen,
  // which is every real one. Ordered by how often each has paid off on actual camera files.
  for (const scale of [2, 3, 1]) {
    const hit = read(toRgba(scale === 1 ? pbm : scaleNearest(pbm, scale), 1));
    if (hit) return hit;
  }
  for (const radius of [1, 2]) {
    for (const scale of [2, 3]) {
      const hit = read(blurred(pbm, radius, scale));
      if (hit) return hit;
    }
  }
  // A binary median kills isolated speckle where the grey average only softens it.
  const cleaned = majority3x3(pbm);
  for (const scale of [2, 3, 1]) {
    const hit = read(toRgba(scale === 1 ? cleaned : scaleNearest(cleaned, scale), 1));
    if (hit) return hit;
  }
  return null;
}
