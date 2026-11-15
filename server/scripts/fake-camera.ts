// `pnpm fake-camera --fixture moon` — pretend to be the bridge app.
//
// Says hello as a camera, then asks for an address, names a blog or uploads
// one of the fixture photos. Enough to walk the whole registration flow on a
// laptop without hardware.
//
//   pnpm fake-camera --email mees@example.com    # POST /claim, then open the link in /app/dev/mailbox
//   pnpm fake-camera --blog mees --name Mees     # POST /blog for the confirmed address
//   pnpm fake-camera --fixture moon              # post a picture
//   pnpm fake-camera --fixture moon --index 12   # with a specific camera index
//   pnpm fake-camera --status                    # GET /status
//   pnpm fake-camera --firmware 0.1.0 --status   # pretend to be running older firmware
//
// Options: --server http://localhost:3000  --id 7cdfa1e2b3c4  --secret <32 hex>
//          --firmware 0.2.0 (what the bridge would have read out of Info)

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

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
const firmware = args.get('firmware');
const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), '..', '..', 'packages', 'pbm', 'fixtures');

async function main() {
  const hello = await fetch(`${server}/api/camera/hello`, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ camera_id: id, secret, ...(firmware ? { firmware } : {}) }),
  });
  console.log('hello', hello.status, await hello.text());
  if (!hello.ok) process.exit(1);

  if (args.has('status')) {
    const r = await fetch(`${server}/api/camera/status`, { headers: auth });
    console.log('status', r.status, await r.text());
    return;
  }

  const post = (path: string, body: unknown) =>
    fetch(`${server}/api/camera/${path}`, {
      method: 'POST',
      headers: { ...auth, 'content-type': 'application/json' },
      body: JSON.stringify(body),
    });
  if (args.has('email')) {
    const r = await post('claim', { email: args.get('email') });
    console.log('claim', r.status, await r.text());
    return;
  }
  if (args.has('blog')) {
    const handle = args.get('blog')!;
    const r = await post('blog', { handle, name: args.get('name') ?? handle });
    console.log('blog', r.status, await r.text());
    return;
  }

  if (!args.has('fixture')) {
    console.error('need --email <address>, --blog <handle>, --fixture <name> or --status');
    process.exit(2);
  }
  const bytes = new Uint8Array(readFileSync(join(FIXTURES, `${args.get('fixture')}.pbm`)));

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
