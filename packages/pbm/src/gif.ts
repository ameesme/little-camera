// Hand-rolled 1-bit animated GIF encoder, for serving motion photos
// (protocol §2.1). The stored PBM stays the photo; this is only how a
// browser, a mail client or the Photos app gets to see it move.
//
// GIF rather than APNG because more places animate it: Gmail and Outlook show
// an APNG's first frame only, and iOS keeps a GIF animated when it is saved to
// Photos. Two palette entries, index 0 white and 1 black, so a PBM bit is its
// own palette index. Pure TypeScript, no dependency.

import { getPixel, type Pbm } from './pbm.js';

/** Frames as a GIF that loops forever, each shown for `ms` (rounded to GIF's 10 ms ticks). */
export function pbmFramesToGif(frames: Pbm[], ms: number): Uint8Array<ArrayBuffer> {
  if (frames.length === 0) throw new Error('gif: no frames');
  const { width, height } = frames[0];
  const out = new ByteWriter();
  const delay = Math.max(1, Math.min(65535, Math.round(ms / 10)));

  out.ascii('GIF89a');
  out.u16(width);
  out.u16(height);
  out.bytes([0x80, 0, 0]); // global colour table of 2 entries, background 0, square pixels
  out.bytes([0xff, 0xff, 0xff, 0x00, 0x00, 0x00]); // 0 white, 1 black
  if (frames.length > 1) {
    // NETSCAPE2.0: loop forever.
    out.bytes([0x21, 0xff, 0x0b]);
    out.ascii('NETSCAPE2.0');
    out.bytes([0x03, 0x01, 0x00, 0x00, 0x00]);
  }

  const indices = new Uint8Array(width * height);
  for (const frame of frames) {
    if (frame.width !== width || frame.height !== height) throw new Error('gif: frames differ in size');
    // Graphic control: dispose "leave in place" (every frame covers the canvas anyway), the delay, no transparency.
    out.bytes([0x21, 0xf9, 0x04, 0x04]);
    out.u16(delay);
    out.bytes([0x00, 0x00]);
    // Image descriptor: the whole canvas, no local table, not interlaced.
    out.bytes([0x2c]);
    out.u16(0);
    out.u16(0);
    out.u16(width);
    out.u16(height);
    out.bytes([0x00]);

    for (let y = 0; y < height; y++) {
      for (let x = 0; x < width; x++) indices[y * width + x] = getPixel(frame, x, y) ? 1 : 0;
    }
    const data = lzw(indices, MIN_CODE_SIZE);
    out.bytes([MIN_CODE_SIZE]);
    for (let i = 0; i < data.length; i += 255) {
      const block = data.subarray(i, i + 255);
      out.bytes([block.length]);
      out.bytes(block);
    }
    out.bytes([0x00]);
  }
  out.bytes([0x3b]);
  return out.finish();
}

// Two colours need one bit, but GIF's LZW starts at two at the least.
const MIN_CODE_SIZE = 2;

/** GIF's variable-width LZW, codes packed LSB first, a clear code when the table fills. */
function lzw(indices: Uint8Array, minCodeSize: number): Uint8Array {
  const clear = 1 << minCodeSize;
  const eoi = clear + 1;
  const bits = new BitWriter();
  let codeSize = minCodeSize + 1;
  let next = eoi + 1;
  // Key: prefix code << 8 | next index. Prefixes stay under 4096, indices under 256.
  const table = new Map<number, number>();

  bits.write(clear, codeSize);
  let prefix = indices[0];
  for (let i = 1; i < indices.length; i++) {
    const k = indices[i];
    const key = (prefix << 8) | k;
    const known = table.get(key);
    if (known !== undefined) {
      prefix = known;
      continue;
    }
    bits.write(prefix, codeSize);
    if (next === 4096) {
      bits.write(clear, codeSize);
      table.clear();
      next = eoi + 1;
      codeSize = minCodeSize + 1;
    } else {
      // The decoder widens one code behind the encoder, so widen before handing out the code that needs it.
      if (next >= 1 << codeSize) codeSize++;
      table.set(key, next++);
    }
    prefix = k;
  }
  bits.write(prefix, codeSize);
  bits.write(eoi, codeSize);
  return bits.finish();
}

class BitWriter {
  private buf: number[] = [];
  private acc = 0;
  private n = 0;

  write(code: number, size: number): void {
    this.acc |= code << this.n;
    this.n += size;
    while (this.n >= 8) {
      this.buf.push(this.acc & 0xff);
      this.acc >>>= 8;
      this.n -= 8;
    }
  }

  finish(): Uint8Array {
    if (this.n > 0) this.buf.push(this.acc & 0xff);
    return Uint8Array.from(this.buf);
  }
}

class ByteWriter {
  private parts: number[] = [];

  bytes(b: ArrayLike<number>): void {
    for (let i = 0; i < b.length; i++) this.parts.push(b[i]);
  }

  ascii(s: string): void {
    for (let i = 0; i < s.length; i++) this.parts.push(s.charCodeAt(i));
  }

  u16(v: number): void {
    this.parts.push(v & 0xff, (v >> 8) & 0xff);
  }

  finish(): Uint8Array<ArrayBuffer> {
    return Uint8Array.from(this.parts);
  }
}
