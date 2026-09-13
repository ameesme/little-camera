import { randomBytes } from 'node:crypto';

const URL_ALPHABET = 'abcdefghijklmnopqrstuvwxyz0123456789';

/**
 * 12 lowercase alphanumerics for photo URLs (`k3m9q2z8x1w4`), ~62 bits.
 * Rejection sampling so every character is equally likely.
 */
export function publicId(len = 12): string {
  let out = '';
  while (out.length < len) {
    for (const b of randomBytes(len * 2)) {
      if (b < 252 && out.length < len) out += URL_ALPHABET[b % 36]; // 252 = 7 × 36
    }
  }
  return out;
}
