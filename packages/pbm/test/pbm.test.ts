import { describe, expect, it } from 'vitest';
import { readFileSync, readdirSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { PNG } from 'pngjs';
import {
  ALPHABET,
  blankPbm,
  cropSquare,
  encodePbm,
  encodePbmFrames,
  getPixel,
  parsePbm,
  parsePbmFrames,
  pbmFramesToGif,
  pbmToPng,
  pbmToPngScaled,
  scaleNearest,
  setPixel,
  shortCode,
  toGray,
} from '../src/index.js';

const fixturesDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'fixtures');
const fixtureNames = readdirSync(fixturesDir).filter((f) => f.endsWith('.pbm'));
const fixture = (name: string) => new Uint8Array(readFileSync(join(fixturesDir, name)));

const ascii = (s: string) => new Uint8Array([...s].map((c) => c.charCodeAt(0)));
const concat = (...parts: Uint8Array[]) => {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0));
  let off = 0;
  for (const p of parts) {
    out.set(p, off);
    off += p.length;
  }
  return out;
};

describe('parsePbm on the fixtures', () => {
  it('finds eight fixtures', () => {
    expect(fixtureNames.length).toBe(8);
  });

  for (const name of fixtureNames) {
    it(`parses ${name}: 320×240, 9600 bytes, no meta, byte-identical round trip`, () => {
      const bytes = fixture(name);
      const pbm = parsePbm(bytes);
      expect(pbm.width).toBe(320);
      expect(pbm.height).toBe(240);
      expect(pbm.rows.length).toBe(9600);
      expect(pbm.meta).toEqual({});
      const again = encodePbm(pbm.width, pbm.height, pbm.rows, pbm.meta);
      expect(Buffer.from(again).equals(Buffer.from(bytes))).toBe(true);
      // Real photos are neither blank nor solid black.
      const black = toGray(pbm).filter((v) => v === 0).length;
      expect(black).toBeGreaterThan(1000);
      expect(black).toBeLessThan(320 * 240 - 1000);
    });
  }
});

describe('parsePbm header handling', () => {
  const raster = new Uint8Array(9600).fill(0xa5);

  it('reads the firmware comment line into meta', () => {
    const buf = concat(ascii('P4\n# boot=17 up=48213 t=1757789000\n320 240\n'), raster);
    const pbm = parsePbm(buf);
    expect(pbm.meta).toEqual({ boot: 17, up: 48213, t: 1757789000 });
    expect(pbm.rows).toEqual(raster);
  });

  it('accepts a header without comment', () => {
    const pbm = parsePbm(concat(ascii('P4\n320 240\n'), raster));
    expect(pbm.meta).toEqual({});
    expect(pbm.width).toBe(320);
  });

  it('tolerates odd whitespace and comments between tokens', () => {
    const buf = concat(ascii('P4 # hi\r\n#another\n  320\t240 '), raster);
    const pbm = parsePbm(buf);
    expect([pbm.width, pbm.height]).toEqual([320, 240]);
  });

  it('parses a small odd-width image with row padding', () => {
    const pbm = parsePbm(concat(ascii('P4\n10 2\n'), new Uint8Array([0xff, 0xc0, 0x00, 0x40])));
    expect(getPixel(pbm, 9, 0)).toBe(true);
    expect(getPixel(pbm, 0, 1)).toBe(false);
    expect(getPixel(pbm, 9, 1)).toBe(true);
  });

  it('rejects P1/P5 and short rasters', () => {
    expect(() => parsePbm(concat(ascii('P1\n320 240\n'), raster))).toThrow(/P4/);
    expect(() => parsePbm(concat(ascii('P5\n320 240\n'), raster))).toThrow(/P4/);
    expect(() => parsePbm(concat(ascii('P4\n320 240\n'), raster.subarray(0, 9599)))).toThrow(/short/);
    expect(() => parsePbm(ascii('P4\n320'))).toThrow();
    expect(() => parsePbm(ascii('P4\n320 abc\n'))).toThrow(/height/);
  });
});

describe('encodePbm', () => {
  const raster = new Uint8Array(9600);
  it('writes the exact firmware header with meta', () => {
    const out = encodePbm(320, 240, raster, { boot: 17, up: 48213, t: 1757789000 });
    const header = Buffer.from(out.subarray(0, out.length - 9600)).toString('ascii');
    expect(header).toBe('P4\n# boot=17 up=48213 t=1757789000\n320 240\n');
  });
  it('omits the comment line without meta or with empty meta', () => {
    expect(Buffer.from(encodePbm(320, 240, raster).subarray(0, 11)).toString('ascii')).toBe('P4\n320 240\n');
    expect(Buffer.from(encodePbm(320, 240, raster, {}).subarray(0, 11)).toString('ascii')).toBe('P4\n320 240\n');
  });
  it('refuses a raster of the wrong size', () => {
    expect(() => encodePbm(320, 240, new Uint8Array(10))).toThrow();
  });
});

describe('pbmToPng', () => {
  for (const name of fixtureNames.slice(0, 3)) {
    it(`produces a PNG whose pixels match ${name}`, () => {
      const pbm = parsePbm(fixture(name));
      const png = PNG.sync.read(Buffer.from(pbmToPng(pbm)));
      expect(png.width).toBe(320);
      expect(png.height).toBe(240);
      // pngjs expands to 8-bit RGBA; take the red channel of every pixel.
      const red = new Uint8ClampedArray(png.width * png.height);
      for (let i = 0; i < red.length; i++) red[i] = png.data[i * 4];
      expect(Buffer.from(red).equals(Buffer.from(toGray(pbm)))).toBe(true);
    });
  }

  it('is a 1-bit greyscale PNG and small', () => {
    const pbm = parsePbm(fixture(fixtureNames[0]));
    const bytes = pbmToPng(pbm);
    expect(Array.from(bytes.subarray(0, 8))).toEqual([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);
    // IHDR data starts at byte 16: width(4) height(4) depth colour ...
    expect(bytes[24]).toBe(1);
    expect(bytes[25]).toBe(0);
    expect(bytes.length).toBeLessThan(9600);
  });

  it('scales by an integer factor', () => {
    const pbm = blankPbm(4, 2);
    setPixel(pbm, 1, 0, true);
    const png = PNG.sync.read(Buffer.from(pbmToPngScaled(pbm, 3)));
    expect(png.width).toBe(12);
    expect(png.height).toBe(6);
    // pixel (3..5, 0..2) black, (0, 0) white
    expect(png.data[0]).toBe(255);
    expect(png.data[(1 * 12 + 4) * 4]).toBe(0);
    expect(png.data[(1 * 12 + 6) * 4]).toBe(255);
  });
});

describe('pixel operations', () => {
  it('cropSquare takes the centre 240×240', () => {
    const pbm = parsePbm(fixture(fixtureNames[0]));
    const sq = cropSquare(pbm);
    expect([sq.width, sq.height]).toEqual([240, 240]);
    for (let y = 0; y < 240; y += 7) {
      for (let x = 0; x < 240; x += 5) expect(getPixel(sq, x, y)).toBe(getPixel(pbm, x + 40, y));
    }
    const small = cropSquare(pbm, 100);
    expect(getPixel(small, 0, 0)).toBe(getPixel(pbm, 110, 70));
  });

  it('scaleNearest duplicates pixels', () => {
    const pbm = blankPbm(3, 1);
    setPixel(pbm, 2, 0, true);
    const big = scaleNearest(pbm, 2);
    expect([big.width, big.height]).toEqual([6, 2]);
    expect(getPixel(big, 3, 0)).toBe(false);
    expect(getPixel(big, 4, 1)).toBe(true);
    expect(getPixel(big, 5, 1)).toBe(true);
  });

  it('toGray is one byte a pixel, black as 0', () => {
    const pbm = blankPbm(2, 1);
    setPixel(pbm, 0, 0, true);
    expect(Array.from(toGray(pbm))).toEqual([0, 255]);
  });
});

describe('codes', () => {
  it('uses the 32-character alphabet without 0 O 1 I', () => {
    expect(ALPHABET).toBe('ABCDEFGHJKLMNPQRSTUVWXYZ23456789');
    expect(ALPHABET.length).toBe(32);
  });

  it.each([
    ['7cdfa1e2b3c4', 'MM48F3'],
    ['000000000000', '882TLC'],
    ['ffffffffffff', 'XMPHSF'],
    ['a1b2c3d4e5f6', 'ZZWB7E'],
  ])('shortCode(%s) = %s', (id, code) => {
    expect(shortCode(id)).toBe(code);
  });

});

describe('motion photos (protocol §2.1)', () => {
  const W = 320;
  const H = 240;
  const frame = (fill: number) => new Uint8Array(bytesPerRowOf(W) * H).fill(fill);
  const bytesPerRowOf = (w: number) => (w + 7) >> 3;

  it('round-trips three frames and the ms key', () => {
    const file = encodePbmFrames(W, H, [frame(0x00), frame(0xaa), frame(0xff)], { boot: 3, up: 10, t: 0, ms: 412 });
    const frames = parsePbmFrames(file);
    expect(frames).toHaveLength(3);
    expect(frames[1].rows[0]).toBe(0xaa);
    expect(frames[2].meta.ms).toBe(412);
    // A reader that knows only the first image sees a still.
    expect(parsePbm(file).rows[0]).toBe(0x00);
  });

  it('treats a still as one frame', () => {
    expect(parsePbmFrames(fixture('moon.pbm'))).toHaveLength(1);
  });

  it('keeps the frames before a truncated one', () => {
    const file = encodePbmFrames(W, H, [frame(1), frame(2), frame(3)], { ms: 400 });
    expect(parsePbmFrames(file.subarray(0, file.length - 100))).toHaveLength(2);
  });

  it('encodes a looping GIF that decodes back to every frame', () => {
    const rows = ['cat', 'cup', 'moon'].map((n) => parsePbm(fixture(`${n}.pbm`)).rows);
    const frames = parsePbmFrames(encodePbmFrames(W, H, rows, { ms: 412 }));
    const gif = decodeGif(pbmFramesToGif(frames, 412));
    expect(gif).toMatchObject({ width: W, height: H, loops: true, delays: [41, 41, 41] });
    expect(gif.frames).toHaveLength(3);
    gif.frames.forEach((indices, f) => {
      for (let y = 0; y < H; y++) {
        for (let x = 0; x < W; x++) {
          if (indices[y * W + x] !== (getPixel(frames[f], x, y) ? 1 : 0)) throw new Error(`frame ${f} differs at ${x},${y}`);
        }
      }
    });
  });

  it('survives the LZW table filling up and clearing', () => {
    // Noise defeats the dictionary, so the 4096-code table fills many times over.
    const noise = blankPbm(W, H);
    let seed = 1;
    for (let i = 0; i < noise.rows.length; i++) {
      seed = (seed * 1103515245 + 12345) >>> 0;
      noise.rows[i] = seed >>> 24;
    }
    const gif = decodeGif(pbmFramesToGif([noise], 400));
    expect(gif.loops).toBe(false);
    expect(gif.frames[0][5]).toBe(getPixel(noise, 5, 0) ? 1 : 0);
    expect(Array.from(gif.frames[0]).filter((v, i) => v !== (getPixel(noise, i % W, (i / W) | 0) ? 1 : 0))).toHaveLength(0);
  });
});

/** Just enough of a GIF decoder to check the encoder: one global table, full-canvas frames, no interlace. */
function decodeGif(buf: Uint8Array) {
  const u16 = (i: number) => buf[i] | (buf[i + 1] << 8);
  expect(String.fromCharCode(...buf.subarray(0, 6))).toBe('GIF89a');
  const width = u16(6);
  const height = u16(8);
  let pos = 13 + 3 * (2 << (buf[10] & 7));
  const frames: Uint8Array[] = [];
  const delays: number[] = [];
  let loops = false;
  while (buf[pos] !== 0x3b) {
    if (buf[pos] === 0x21) {
      if (buf[pos + 1] === 0xf9) delays.push(u16(pos + 4));
      if (buf[pos + 1] === 0xff) loops = true;
      pos += 2;
      while (buf[pos] !== 0) pos += buf[pos] + 1;
      pos++;
      continue;
    }
    expect(buf[pos]).toBe(0x2c);
    pos += 10;
    const minCodeSize = buf[pos++];
    const data: number[] = [];
    while (buf[pos] !== 0) {
      data.push(...buf.subarray(pos + 1, pos + 1 + buf[pos]));
      pos += buf[pos] + 1;
    }
    pos++;
    frames.push(lzwDecode(Uint8Array.from(data), minCodeSize, width * height));
  }
  return { width, height, frames, delays, loops };
}

function lzwDecode(data: Uint8Array, minCodeSize: number, count: number): Uint8Array {
  const clear = 1 << minCodeSize;
  const eoi = clear + 1;
  const out = new Uint8Array(count);
  let n = 0;
  let bitPos = 0;
  let codeSize = minCodeSize + 1;
  let table: number[][] = [];
  const reset = () => {
    table = [];
    for (let i = 0; i < clear; i++) table[i] = [i];
    table[clear] = [];
    table[eoi] = [];
    codeSize = minCodeSize + 1;
  };
  reset();
  let prev: number[] | null = null;
  for (;;) {
    let code = 0;
    for (let i = 0; i < codeSize; i++, bitPos++) code |= ((data[bitPos >> 3] >> (bitPos & 7)) & 1) << i;
    if (code === clear) {
      reset();
      prev = null;
      continue;
    }
    if (code === eoi) break;
    let entry: number[];
    if (code < table.length) entry = table[code];
    else if (prev) entry = [...prev, prev[0]];
    else throw new Error('lzw: bad code');
    for (const v of entry) out[n++] = v;
    if (prev && table.length < 4096) table.push([...prev, entry[0]]);
    if (table.length === 1 << codeSize && codeSize < 12) codeSize++;
    prev = entry;
  }
  if (n !== count) throw new Error(`lzw: ${n} pixels, want ${count}`);
  return out;
}
