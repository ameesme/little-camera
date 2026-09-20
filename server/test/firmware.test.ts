// Firmware releases (protocol §4.7): the manifest, what /latest picks, and
// what the download route promises about the bytes.

import { createHash } from 'node:crypto';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { compareVersions } from '../src/lib/firmware.js';
import { apex, cameraGet, hello, makeWorld, type TestWorld } from './helpers.js';

let w: TestWorld;
beforeEach(() => {
  w = makeWorld();
});
afterEach(() => w.cleanup());

/** An "image": the route serves bytes, it has no opinion about what is in them. */
function image(version: string, size = 8192): Uint8Array {
  const bytes = new Uint8Array(size);
  for (let i = 0; i < size; i++) bytes[i] = (i * 31 + version.length) & 0xff;
  return bytes;
}

interface Entry {
  version: string;
  channel?: string;
  hardware?: string;
  file?: string;
  notes?: string;
  released_at?: number;
  bytes?: Uint8Array | null;
}

function publish(entries: Entry[]): void {
  const dir = w.env.config.firmwareDir;
  mkdirSync(dir, { recursive: true });
  const releases = entries.map((e) => {
    const file = e.file ?? `little-camera-${e.version}.bin`;
    const bytes = e.bytes === null ? null : (e.bytes ?? image(e.version));
    // A manifest entry naming a path is the case under test; writing one here
    // would only mean this helper doing the traversal instead of the server.
    if (bytes && !file.includes('/')) writeFileSync(join(dir, file), bytes);
    return {
      version: e.version,
      channel: e.channel ?? 'stable',
      hardware: e.hardware ?? 'xiao_shutter',
      file,
      notes: e.notes ?? null,
      released_at: e.released_at ?? 1_757_789_000,
    };
  });
  writeFileSync(join(dir, 'manifest.json'), JSON.stringify({ releases }, null, 2));
}

const get = (path: string) => w.app.request(apex(path));

describe('version comparison', () => {
  it('compares dotted numbers, not strings', () => {
    expect(compareVersions('0.10.0', '0.9.9')).toBe(1);
    expect(compareVersions('1.0.0', '0.99.99')).toBe(1);
    expect(compareVersions('0.2.0', '0.2.0')).toBe(0);
    expect(compareVersions('0.2.0', '0.2.1')).toBe(-1);
  });
});

describe('GET /api/firmware/latest', () => {
  it('404s with nothing published', async () => {
    const res = await get('/api/firmware/latest');
    expect(res.status).toBe(404);
    expect((await res.json()).error).toBe('no_release');
  });

  it('picks the newest of a channel and hashes the file on disk', async () => {
    publish([{ version: '0.2.0' }, { version: '0.10.0' }, { version: '0.9.9' }]);
    const res = await get('/api/firmware/latest');
    expect(res.status).toBe(200);
    const body = await res.json();
    expect(body.version).toBe('0.10.0');
    expect(body.hardware).toBe('xiao_shutter');
    expect(body.size).toBe(8192);
    // The digest is the file's, never the manifest's — that is what the camera
    // checks the image against once the phone has carried it (§3.7).
    expect(body.sha256).toBe(createHash('sha256').update(image('0.10.0')).digest('hex'));
    expect(body.url).toBe('http://localhost:3000/api/firmware/xiao_shutter/0.10.0.bin');
    expect(body.update_available).toBe(true);
  });

  it('keeps channels and hardware apart', async () => {
    publish([
      { version: '0.2.0' },
      { version: '0.3.0', channel: 'beta' },
      { version: '9.0.0', hardware: 'other_board' },
    ]);
    expect((await (await get('/api/firmware/latest')).json()).version).toBe('0.2.0');
    expect((await (await get('/api/firmware/latest?channel=beta')).json()).version).toBe('0.3.0');
    expect((await (await get('/api/firmware/latest?hardware=other_board')).json()).version).toBe('9.0.0');
    expect((await get('/api/firmware/latest?channel=nope')).status).toBe(404);
    expect((await get('/api/firmware/latest?channel=Not A Channel')).status).toBe(400);
  });

  it('answers update_available against the installed version', async () => {
    publish([{ version: '0.2.0' }]);
    expect((await (await get('/api/firmware/latest?installed=0.1.0')).json()).update_available).toBe(true);
    expect((await (await get('/api/firmware/latest?installed=0.2.0')).json()).update_available).toBe(false);
    expect((await (await get('/api/firmware/latest?installed=0.3.0')).json()).update_available).toBe(false);
    // Junk is the same as not saying: here is the release, decide yourself.
    expect((await (await get('/api/firmware/latest?installed=banana')).json()).update_available).toBe(true);
  });

  it('ignores manifest entries that are not backed by a file, or that name a path', async () => {
    publish([
      { version: '0.2.0' },
      { version: '0.4.0', bytes: null },
      { version: '0.5.0', file: '../../etc/passwd' },
    ]);
    expect((await (await get('/api/firmware/latest')).json()).version).toBe('0.2.0');
    expect((await get('/api/firmware/xiao_shutter/0.5.0.bin')).status).toBe(404);
  });
});

describe('GET /api/firmware/<hardware>/<version>.bin', () => {
  it('serves the image, cacheable and revalidatable', async () => {
    publish([{ version: '0.2.0' }]);
    const res = await get('/api/firmware/xiao_shutter/0.2.0.bin');
    expect(res.status).toBe(200);
    expect(res.headers.get('content-type')).toBe('application/octet-stream');
    const bytes = new Uint8Array(await res.arrayBuffer());
    expect(bytes).toEqual(image('0.2.0'));
    const etag = res.headers.get('etag')!;
    expect(etag).toBe(`"${createHash('sha256').update(bytes).digest('hex')}"`);

    const again = await w.app.request(apex('/api/firmware/xiao_shutter/0.2.0.bin', { headers: { 'if-none-match': etag } }));
    expect(again.status).toBe(304);
  });

  it('404s on an unknown version, hardware or shape', async () => {
    publish([{ version: '0.2.0' }]);
    expect((await get('/api/firmware/xiao_shutter/0.3.0.bin')).status).toBe(404);
    expect((await get('/api/firmware/other_board/0.2.0.bin')).status).toBe(404);
    expect((await get('/api/firmware/xiao_shutter/0.2.0.txt')).status).toBe(404);
    expect((await get('/api/firmware/xiao_shutter/manifest.json')).status).toBe(404);
  });

  it('needs no camera credentials: an image is not a secret', async () => {
    publish([{ version: '0.2.0' }]);
    expect((await get('/api/firmware/latest')).status).toBe(200);
    expect((await get('/api/firmware/xiao_shutter/0.2.0.bin')).status).toBe(200);
  });
});

describe('the camera API side of it', () => {
  it('records what the phone reports and offers an update on /status', async () => {
    publish([{ version: '0.3.0', notes: 'Faster chirps.' }]);

    const first = await hello(w);
    expect(first.status).toBe(200);
    // Nothing reported yet: a release to look at, but no claim about it.
    const before = await cameraGet(w, '/status');
    expect(before.body.firmware.installed).toBeNull();
    expect(before.body.firmware.latest.version).toBe('0.3.0');
    expect(before.body.firmware.update_available).toBe(false);

    const said = await w.app.request(
      apex('/api/camera/hello', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ camera_id: '7cdfa1e2b3c4', secret: '9f2c0000111122223333444455556666', firmware: '0.2.0' }),
      }),
    );
    expect(said.status).toBe(200);

    const after = await cameraGet(w, '/status');
    expect(after.body.firmware.installed).toBe('0.2.0');
    expect(after.body.firmware.update_available).toBe(true);
    expect(after.body.firmware.latest.notes).toBe('Faster chirps.');
  });

  it('refuses a firmware string that is not a version', async () => {
    const res = await w.app.request(
      apex('/api/camera/hello', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ camera_id: '7cdfa1e2b3c4', secret: '9f2c0000111122223333444455556666', firmware: 'nightly' }),
      }),
    );
    expect(res.status).toBe(400);
  });
});
