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
  getPixel,
  majority3x3,
  parsePbm,
  pbmToPng,
  pbmToPngScaled,
  randomCode,
  scaleNearest,
  setPixel,
  shortCode,
  toGray,
  toRgba,
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

  it('majority3x3 removes a lone speckle and fills a lone hole', () => {
    const pbm = blankPbm(9, 9);
    setPixel(pbm, 4, 4, true); // lone black dot on white
    for (let y = 0; y < 3; y++) for (let x = 0; x < 3; x++) setPixel(pbm, x, y, true);
    setPixel(pbm, 1, 1, false); // lone white hole in a black block
    const f = majority3x3(pbm);
    expect(getPixel(f, 4, 4)).toBe(false);
    expect(getPixel(f, 1, 1)).toBe(true);
  });

  it('toGray and toRgba agree', () => {
    const pbm = blankPbm(2, 1);
    setPixel(pbm, 0, 0, true);
    expect(Array.from(toGray(pbm))).toEqual([0, 255]);
    const rgba = toRgba(pbm, 2);
    expect([rgba.width, rgba.height]).toEqual([4, 2]);
    expect(Array.from(rgba.data.subarray(0, 8))).toEqual([0, 0, 0, 255, 0, 0, 0, 255]);
    expect(Array.from(rgba.data.subarray(8, 12))).toEqual([255, 255, 255, 255]);
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

  it('randomCode draws from the alphabet', () => {
    const c = randomCode(6);
    expect(c).toHaveLength(6);
    expect(c).toMatch(/^[A-Z2-9]{6}$/);
    for (const ch of c) expect(ALPHABET).toContain(ch);
    expect(randomCode(6)).not.toBe(randomCode(6));
  });
});
