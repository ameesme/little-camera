// `pnpm fake-camera --code LC:XXXXXX` — pretend to be the bridge app.
//
// Says hello as a camera, then uploads either a simulated photo of the
// verification QR (--code) or one of the fixture photos (--fixture cat).
// Enough to walk the whole registration flow on a laptop without hardware.
//
//   pnpm fake-camera --code LC:KQ7M2X            # bind the camera to /me
//   pnpm fake-camera --fixture moon              # post a picture
//   pnpm fake-camera --fixture moon --index 12   # with a specific camera index
//   pnpm fake-camera --status                    # GET /status
//
// Options: --server http://localhost:3000  --id 7cdfa1e2b3c4  --secret <32 hex>

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { encodePbm } from '@little-camera/pbm';
import { fakeCameraShot } from '../src/lib/simulate.js';

const args = new Map<string, string>();
for (let i = 2; i < process.argv.length; i++) {
  const a = process.argv[i];
  if (!a.startsWith('--')) continue;
  const next = process.argv[i + 1];
  if (next && !next.startsWith('--')) {
    args.set(a.slice(2), next);
    i++;
  } else args.set(a.slice(2), 'true');
}

const server = (args.get('server') ?? 'http://localhost:3000').replace(/\/$/, '');
const id = args.get('id') ?? '7cdfa1e2b3c4';
const secret = args.get('secret') ?? '9f2c0000111122223333444455556666';
const auth = { authorization: `Camera ${id}:${secret}` };
const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'packages', 'pbm', 'fixtures');

async function main() {
  const hello = await fetch(`${server}/api/camera/hello`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ camera_id: id, secret }),
  });
  console.log('hello', hello.status, await hello.text());
  if (!hello.ok) process.exit(1);

  if (args.has('status')) {
    const r = await fetch(`${server}/api/camera/status`, { headers: auth });
    console.log('status', r.status, await r.text());
    return;
  }

  let bytes: Uint8Array<ArrayBuffer>;
  if (args.has('code')) {
    let code = args.get('code')!.toUpperCase();
    if (!code.startsWith('LC:')) code = `LC:${code}`;
    const shot = fakeCameraShot(code, { modulePx: 8, blur: 1, offsetX: 9, offsetY: -5 });
    bytes = encodePbm(shot.width, shot.height, shot.rows, { boot: 1, up: 12345, t: 0 });
  } else if (args.has('fixture')) {
    bytes = new Uint8Array(readFileSync(join(FIXTURES, `${args.get('fixture')}.pbm`)));
  } else {
    console.error('need --code LC:XXXXXX, --fixture <name> or --status');
    process.exit(2);
  }

  const index = args.get('index') ?? String(Math.floor(Date.now() / 1000) % 60000);
  const r = await fetch(`${server}/api/camera/photos`, {
    method: 'POST',
    headers: {
      ...auth,
      'content-type': 'image/x-portable-bitmap',
      'x-photo-index': index,
      'x-captured-at': String(Math.floor(Date.now() / 1000)),
      'x-captured-at-source': 'phone',
    },
    body: bytes,
  });
  console.log('photo', r.status, await r.text());
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
