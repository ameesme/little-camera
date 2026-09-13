// Hand-rolled 1-bit greyscale PNG encoder.
//
// A PNG with bit depth 1 and colour type 0 stores exactly the same packed
// rows as a PBM, except that in PNG 1 means WHITE. So encoding is: invert
// the bits, prefix each row with a filter byte of 0, deflate, wrap in
// chunks. No dependency, and the output is tiny (a photo is ~3 KB).

import { deflateSync } from 'node:zlib';
import { bytesPerRow, scaleNearest, type Pbm } from './pbm.js';

const CRC_TABLE = (() => {
  const table = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    table[n] = c >>> 0;
  }
  return table;
})();

/** IEEE CRC-32, as used by PNG chunks (and by the camera's BLE END frame). */
export function crc32(bytes: Uint8Array, seed = 0): number {
  let c = (seed ^ 0xffffffff) >>> 0;
  for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

function chunk(type: string, data: Uint8Array): Uint8Array {
  const out = new Uint8Array(12 + data.length);
  const view = new DataView(out.buffer);
  view.setUint32(0, data.length);
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i);
  out.set(data, 8);
  view.setUint32(8 + data.length, crc32(out.subarray(4, 8 + data.length)));
  return out;
}

const SIGNATURE = new Uint8Array([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

/** Encode a PBM as a 1-bit greyscale PNG (black stays black). */
export function pbmToPng(pbm: Pbm): Uint8Array {
  const stride = bytesPerRow(pbm.width);

  const ihdr = new Uint8Array(13);
  const iv = new DataView(ihdr.buffer);
  iv.setUint32(0, pbm.width);
  iv.setUint32(4, pbm.height);
  ihdr[8] = 1; // bit depth
  ihdr[9] = 0; // colour type: greyscale
  ihdr[10] = 0; // compression
  ihdr[11] = 0; // filter method
  ihdr[12] = 0; // no interlace

  // Filter byte 0 (None) per row, then the row with bits inverted. Padding
  // bits in the last byte do not matter to decoders but we invert them too.
  const raw = new Uint8Array((stride + 1) * pbm.height);
  for (let y = 0; y < pbm.height; y++) {
    const src = y * stride;
    const dst = y * (stride + 1);
    raw[dst] = 0;
    for (let i = 0; i < stride; i++) raw[dst + 1 + i] = ~pbm.rows[src + i] & 0xff;
  }
  const idat = new Uint8Array(deflateSync(raw, { level: 9 }));

  const parts = [SIGNATURE, chunk('IHDR', ihdr), chunk('IDAT', idat), chunk('IEND', new Uint8Array(0))];
  const total = parts.reduce((n, p) => n + p.length, 0);
  const out = new Uint8Array(total);
  let off = 0;
  for (const p of parts) {
    out.set(p, off);
    off += p.length;
  }
  return out;
}

/** Nearest-neighbour upscale then encode; still 1-bit, still crisp. */
export function pbmToPngScaled(pbm: Pbm, factor: number): Uint8Array {
  return pbmToPng(scaleNearest(pbm, factor));
}
