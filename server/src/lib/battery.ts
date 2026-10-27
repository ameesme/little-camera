// The blog's battery indicator is theatre: the camera reads VBAT but never sends it, Info has no field for it (docs/protocol.md §3.1), so this is a deterministic curve that is stable across requests and servers.

const CYCLE = 7 * 24 * 3600;

/** 32-bit FNV-1a over the UTF-8 bytes of `s`. */
export function fnv1a(s: string): number {
  let h = 0x811c9dc5;
  for (const byte of Buffer.from(s, 'utf8')) {
    h ^= byte;
    h = Math.imul(h, 0x01000193) >>> 0;
  }
  return h >>> 0;
}

/** Percentage 1..100 for `cameraId` bound at `boundAt`, evaluated at `now` (unix seconds). */
export function batteryPercent(cameraId: string, boundAt: number, now: number): number {
  const elapsed = Math.max(0, now - boundAt) % CYCLE;
  const base = 100 - 95 * (elapsed / CYCLE); // 100 → 5 over a week
  const day = Math.floor(now / 86400);
  const jitter = (fnv1a(`${cameraId}:${day}`) % 7) - 3; // -3 … +3, fixed for the day
  return Math.min(100, Math.max(1, Math.round(base + jitter)));
}
