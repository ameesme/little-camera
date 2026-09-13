// Human-readable codes: the camera's short code and the verification code
// on the QR page. See docs/protocol.md §1.

import { createHash } from 'node:crypto';

/** 32 characters, no 0 O 1 I. Index 0 = A … index 31 = 9. */
export const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';

/**
 * Six characters derived from the camera id: the top 30 bits of
 * sha256(camera_id), in 5-bit groups, looked up in ALPHABET. Matches the
 * C and Swift implementations; test vectors live in the protocol doc.
 */
export function shortCode(cameraId: string): string {
  const h = createHash('sha256').update(cameraId, 'ascii').digest();
  // v = first 40 bits, big-endian. BigInt because 40 bits overflow the
  // 32-bit integers JavaScript's shift operators work on.
  let v = 0n;
  for (let i = 0; i < 5; i++) v = (v << 8n) | BigInt(h[i]);
  let code = '';
  for (let i = 0; i < 6; i++) {
    const shift = BigInt(35 - 5 * i);
    code += ALPHABET[Number((v >> shift) & 31n)];
  }
  return code;
}

/**
 * Random code from ALPHABET. 256 is a multiple of 32 so masking a random
 * byte is uniform; no rejection sampling needed.
 */
export function randomCode(len: number): string {
  const bytes = new Uint8Array(len);
  crypto.getRandomValues(bytes);
  let code = '';
  for (let i = 0; i < len; i++) code += ALPHABET[bytes[i] & 31];
  return code;
}
