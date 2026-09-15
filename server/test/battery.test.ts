import { describe, expect, it } from 'vitest';
import { batteryPercent, fnv1a } from '../src/lib/battery.js';

const DAY = 86400;

describe('battery curve', () => {
  it('fnv1a matches the reference value for "a"', () => {
    expect(fnv1a('')).toBe(0x811c9dc5);
    expect(fnv1a('a')).toBe(0xe40c292c);
  });

  it('starts near 100, ends near 5, resets after a week', () => {
    const bound = 1_757_789_000;
    const id = '7cdfa1e2b3c4';
    expect(batteryPercent(id, bound, bound)).toBeGreaterThanOrEqual(97);
    expect(batteryPercent(id, bound, bound + 7 * DAY - 60)).toBeLessThanOrEqual(8);
    expect(batteryPercent(id, bound, bound + 7 * DAY - 60)).toBeGreaterThanOrEqual(1);
    expect(batteryPercent(id, bound, bound + 7 * DAY + 60)).toBeGreaterThanOrEqual(97);
    const mid = batteryPercent(id, bound, bound + 3.5 * DAY);
    expect(mid).toBeGreaterThanOrEqual(49);
    expect(mid).toBeLessThanOrEqual(56);
  });

  it('is monotonic within a day and deterministic', () => {
    const bound = 1_757_789_000;
    const id = 'a1b2c3d4e5f6';
    let last = 101;
    // Stay inside one UTC day so the jitter term is constant.
    const dayStart = Math.ceil(bound / DAY) * DAY;
    for (let t = dayStart; t < dayStart + DAY; t += 3600) {
      const v = batteryPercent(id, bound, t);
      expect(v).toBeLessThanOrEqual(last);
      expect(v).toBe(batteryPercent(id, bound, t));
      last = v;
    }
  });

  it('jitters by at most ±3 between cameras on the same day', () => {
    const bound = 1_757_789_000;
    const t = bound + 2 * DAY;
    const values = ['000000000000', 'ffffffffffff', '7cdfa1e2b3c4', 'a1b2c3d4e5f6'].map((id) =>
      batteryPercent(id, bound, t),
    );
    expect(Math.max(...values) - Math.min(...values)).toBeLessThanOrEqual(6);
    expect(new Set(values).size).toBeGreaterThan(1);
  });
});
