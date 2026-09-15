// A pretend camera shot of the verification page, for tests and the
// `pnpm fake-camera` script. Renders the QR as grey pixels the way a lens
// would see a phone screen (a little soft, a slightly grey surround), then
// dithers it to 1 bit the way the firmware does. Not a physical model,
// just enough noise to prove the decoder copes with dither speckle.

import QRCode from 'qrcode';
import { blankPbm, setPixel, type Pbm } from '@little-camera/pbm';

export interface ShotOptions {
  /** Pixels per QR module in the shot. A phone held at arm's length gives 6–10. */
  modulePx?: number;
  /** Box blur radius in pixels, 0 = razor sharp. */
  blur?: number;
  /** Grey value (0–255) of the world outside the phone screen. */
  surround?: number;
  /** Extra offset of the code from the centre, to break symmetry. */
  offsetX?: number;
  offsetY?: number;
}

export function fakeCameraShot(payload: string, opts: ShotOptions = {}): Pbm {
  const modulePx = opts.modulePx ?? 8;
  const blur = opts.blur ?? 1;
  const surround = opts.surround ?? 150;
  const W = 320;
  const H = 240;

  const qr = QRCode.create(payload, { errorCorrectionLevel: 'H' });
  const n = qr.modules.size;
  const quiet = 4; // modules of white around the code, as on the web page
  const screen = (n + 2 * quiet) * modulePx; // the white "phone screen"
  const ox = ((W - screen) >> 1) + (opts.offsetX ?? 0);
  const oy = ((H - screen) >> 1) + (opts.offsetY ?? 0);

  // 1. grey canvas
  const gray = new Float32Array(W * H).fill(surround);
  for (let y = 0; y < screen; y++) {
    for (let x = 0; x < screen; x++) {
      const px = ox + x;
      const py = oy + y;
      if (px < 0 || py < 0 || px >= W || py >= H) continue;
      const mx = Math.floor(x / modulePx) - quiet;
      const my = Math.floor(y / modulePx) - quiet;
      const dark = mx >= 0 && my >= 0 && mx < n && my < n && qr.modules.get(my, mx) === 1;
      gray[py * W + px] = dark ? 10 : 245; // a screen is never perfectly black or white
    }
  }

  // 2. box blur (separable)
  const blurred = blur > 0 ? boxBlur(gray, W, H, blur) : gray;

  // 3. Floyd–Steinberg to 1 bit
  const pbm = blankPbm(W, H);
  const buf = Float32Array.from(blurred);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const i = y * W + x;
      const old = buf[i];
      const black = old < 128;
      if (black) setPixel(pbm, x, y, true);
      const err = old - (black ? 0 : 255);
      if (x + 1 < W) buf[i + 1] += (err * 7) / 16;
      if (y + 1 < H) {
        if (x > 0) buf[i + W - 1] += (err * 3) / 16;
        buf[i + W] += (err * 5) / 16;
        if (x + 1 < W) buf[i + W + 1] += (err * 1) / 16;
      }
    }
  }
  return pbm;
}

function boxBlur(src: Float32Array, w: number, h: number, r: number): Float32Array {
  const tmp = new Float32Array(w * h);
  const out = new Float32Array(w * h);
  const k = 2 * r + 1;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let s = 0;
      for (let d = -r; d <= r; d++) s += src[y * w + Math.min(w - 1, Math.max(0, x + d))];
      tmp[y * w + x] = s / k;
    }
  }
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      let s = 0;
      for (let d = -r; d <= r; d++) s += tmp[Math.min(h - 1, Math.max(0, y + d)) * w + x];
      out[y * w + x] = s / k;
    }
  }
  return out;
}
