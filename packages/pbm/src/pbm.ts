// PBM (P4) reading and writing, plus the handful of pixel operations the
// server needs. Everything works on the packed 1-bit raster the firmware
// writes: rows of ceil(width/8) bytes, MSB first, a SET bit is BLACK.
//
// Pure TypeScript on purpose: no Node-only APIs in this file, so the same
// code could run in a browser or a test without shims.

export interface PbmMeta {
  /** Boot counter (NVS) at capture time. */
  boot?: number;
  /** millis() at capture time. */
  up?: number;
  /** Unix time at capture time, 0 when the camera did not know the time. */
  t?: number;
}

export interface Pbm {
  width: number;
  height: number;
  /** Packed raster, `bytesPerRow(width) * height` bytes, set bit = black. */
  rows: Uint8Array;
  meta: PbmMeta;
}

export function bytesPerRow(width: number): number {
  return (width + 7) >> 3;
}

const isSpace = (b: number) => b === 0x20 || b === 0x09 || b === 0x0a || b === 0x0d || b === 0x0b || b === 0x0c;

/**
 * Parse a binary PBM.
 *
 * Tolerant per the PBM spec: any amount of whitespace between tokens and
 * `#` comments anywhere between tokens. Comments of the form
 * `# key=value key=value` are folded into `meta` (the firmware writes
 * `# boot=17 up=48213 t=1757789000`). Throws on anything but P4, on a
 * missing or non-numeric dimension, and on a raster that is too short.
 * Trailing bytes after the raster are ignored.
 */
export function parsePbm(buf: Uint8Array): Pbm {
  let pos = 0;
  const meta: PbmMeta = {};

  // A comment runs to the end of the line. Parse `key=value` pairs out of it
  // because that is the only structured thing the firmware puts there.
  const skipComment = () => {
    let end = pos;
    while (end < buf.length && buf[end] !== 0x0a && buf[end] !== 0x0d) end++;
    const text = asciiSlice(buf, pos + 1, end);
    for (const pair of text.trim().split(/\s+/)) {
      const eq = pair.indexOf('=');
      if (eq <= 0) continue;
      const key = pair.slice(0, eq);
      const value = Number(pair.slice(eq + 1));
      if (!Number.isFinite(value)) continue;
      if (key === 'boot' || key === 'up' || key === 't') meta[key] = value;
    }
    pos = end;
  };

  // Skip whitespace and comments up to the next token.
  const skipToToken = () => {
    for (;;) {
      while (pos < buf.length && isSpace(buf[pos])) pos++;
      if (pos < buf.length && buf[pos] === 0x23 /* # */) skipComment();
      else return;
    }
  };

  const readToken = (): string => {
    skipToToken();
    const start = pos;
    while (pos < buf.length && !isSpace(buf[pos]) && buf[pos] !== 0x23) pos++;
    if (pos === start) throw new Error('pbm: unexpected end of header');
    return asciiSlice(buf, start, pos);
  };

  if (readToken() !== 'P4') throw new Error('pbm: not a P4 (binary PBM) file');
  const width = parseDimension(readToken(), 'width');
  const height = parseDimension(readToken(), 'height');

  // Exactly one whitespace byte separates the height from the raster; a
  // comment is technically allowed before it, so tolerate that too.
  if (pos < buf.length && buf[pos] === 0x23) skipComment();
  if (pos >= buf.length || !isSpace(buf[pos])) throw new Error('pbm: missing whitespace before raster');
  pos++;

  const size = bytesPerRow(width) * height;
  if (buf.length - pos < size) throw new Error(`pbm: raster too short (${buf.length - pos} < ${size})`);
  const rows = buf.slice(pos, pos + size);
  return { width, height, rows, meta };
}

function parseDimension(token: string, name: string): number {
  if (!/^\d+$/.test(token)) throw new Error(`pbm: bad ${name} "${token}"`);
  const n = Number(token);
  if (n <= 0) throw new Error(`pbm: bad ${name} ${n}`);
  return n;
}

function asciiSlice(buf: Uint8Array, start: number, end: number): string {
  let s = '';
  for (let i = start; i < end; i++) s += String.fromCharCode(buf[i]);
  return s;
}

/**
 * Encode a binary PBM. With `meta` the header is exactly what the firmware
 * writes (`P4\n# boot=.. up=.. t=..\n320 240\n`); without it (or with an
 * empty object) the comment line is omitted, which is what older firmware
 * produced. Only keys that are present are written, in boot, up, t order.
 */
export function encodePbm(width: number, height: number, rows: Uint8Array, meta?: PbmMeta): Uint8Array<ArrayBuffer> {
  const size = bytesPerRow(width) * height;
  if (rows.length !== size) throw new Error(`pbm: rows must be ${size} bytes, got ${rows.length}`);
  const parts: string[] = [];
  if (meta) {
    if (meta.boot !== undefined) parts.push(`boot=${meta.boot}`);
    if (meta.up !== undefined) parts.push(`up=${meta.up}`);
    if (meta.t !== undefined) parts.push(`t=${meta.t}`);
  }
  const header = 'P4\n' + (parts.length ? `# ${parts.join(' ')}\n` : '') + `${width} ${height}\n`;
  const out = new Uint8Array(header.length + size);
  for (let i = 0; i < header.length; i++) out[i] = header.charCodeAt(i);
  out.set(rows, header.length);
  return out;
}

/** True when the pixel at (x, y) is black. No bounds check. */
export function getPixel(pbm: Pbm, x: number, y: number): boolean {
  const byte = pbm.rows[y * bytesPerRow(pbm.width) + (x >> 3)];
  return (byte & (0x80 >> (x & 7))) !== 0;
}

export function setPixel(pbm: Pbm, x: number, y: number, black: boolean): void {
  const i = y * bytesPerRow(pbm.width) + (x >> 3);
  const mask = 0x80 >> (x & 7);
  if (black) pbm.rows[i] |= mask;
  else pbm.rows[i] &= ~mask;
}

export function blankPbm(width: number, height: number, meta: PbmMeta = {}): Pbm {
  return { width, height, rows: new Uint8Array(bytesPerRow(width) * height), meta };
}

/** Centre crop to a square of `size` (default 240, the full height of a camera photo). */
export function cropSquare(pbm: Pbm, size = 240): Pbm {
  if (size > pbm.width || size > pbm.height) throw new Error('cropSquare: size larger than the image');
  const x0 = (pbm.width - size) >> 1;
  const y0 = (pbm.height - size) >> 1;
  const out = blankPbm(size, size, { ...pbm.meta });
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      if (getPixel(pbm, x0 + x, y0 + y)) setPixel(out, x, y, true);
    }
  }
  return out;
}

/** Integer nearest-neighbour upscale: every pixel becomes a factor×factor block. */
export function scaleNearest(pbm: Pbm, factor: number): Pbm {
  if (!Number.isInteger(factor) || factor < 1) throw new Error('scaleNearest: factor must be a positive integer');
  if (factor === 1) return { ...pbm, rows: pbm.rows.slice(), meta: { ...pbm.meta } };
  const out = blankPbm(pbm.width * factor, pbm.height * factor, { ...pbm.meta });
  for (let y = 0; y < pbm.height; y++) {
    for (let x = 0; x < pbm.width; x++) {
      if (!getPixel(pbm, x, y)) continue;
      for (let dy = 0; dy < factor; dy++) {
        for (let dx = 0; dx < factor; dx++) setPixel(out, x * factor + dx, y * factor + dy, true);
      }
    }
  }
  return out;
}

/**
 * 3×3 majority filter: a pixel becomes black when at least 5 of the 9 pixels
 * around it (itself included, edges clamped) are black. Removes the isolated
 * dither speckles that confuse a QR decoder without moving edges much.
 */
export function majority3x3(pbm: Pbm): Pbm {
  const { width, height } = pbm;
  const out = blankPbm(width, height, { ...pbm.meta });
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let black = 0;
      for (let dy = -1; dy <= 1; dy++) {
        const yy = Math.min(height - 1, Math.max(0, y + dy));
        for (let dx = -1; dx <= 1; dx++) {
          const xx = Math.min(width - 1, Math.max(0, x + dx));
          if (getPixel(pbm, xx, yy)) black++;
        }
      }
      if (black >= 5) setPixel(out, x, y, true);
    }
  }
  return out;
}

/** One byte per pixel, 0 = black, 255 = white. */
export function toGray(pbm: Pbm): Uint8ClampedArray {
  const out = new Uint8ClampedArray(pbm.width * pbm.height);
  for (let y = 0; y < pbm.height; y++) {
    for (let x = 0; x < pbm.width; x++) out[y * pbm.width + x] = getPixel(pbm, x, y) ? 0 : 255;
  }
  return out;
}

/**
 * RGBA buffer (what jsQR and canvases want), optionally upscaled by an
 * integer factor. Returns the buffer and its dimensions.
 */
export function toRgba(pbm: Pbm, scale = 1): { data: Uint8ClampedArray; width: number; height: number } {
  const width = pbm.width * scale;
  const height = pbm.height * scale;
  const data = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y++) {
    const sy = (y / scale) | 0;
    for (let x = 0; x < width; x++) {
      const v = getPixel(pbm, (x / scale) | 0, sy) ? 0 : 255;
      const i = (y * width + x) * 4;
      data[i] = v;
      data[i + 1] = v;
      data[i + 2] = v;
      data[i + 3] = 255;
    }
  }
  return { data, width, height };
}
