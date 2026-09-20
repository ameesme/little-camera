// Firmware releases (protocol §4.7). Mounted at /api/firmware on the apex.
//
// No authentication, deliberately: a firmware image is not a secret, and an
// open URL is one a proxy can cache, a person can curl and a phone can fetch
// before it has ever talked to a camera. What keeps a bad image off a camera
// is not who may download this file — it is the SHA-256 the camera checks
// after the phone has carried the bytes to it (§3.7).

import { Hono } from 'hono';
import type { Env } from '../env.js';
import {
  DEFAULT_CHANNEL,
  DEFAULT_HARDWARE,
  VERSION,
  compareVersions,
  findRelease,
  latestRelease,
  releaseBytes,
  releaseJson,
} from '../lib/firmware.js';

const NAME = /^[a-z0-9][a-z0-9_-]{0,31}$/;
const error = (code: string, message: string) => ({ error: code, message });

export function firmwareRoutes(env: Env): Hono {
  const app = new Hono();

  app.get('/latest', (c) => {
    const hardware = c.req.query('hardware') ?? DEFAULT_HARDWARE;
    const channel = c.req.query('channel') ?? DEFAULT_CHANNEL;
    if (!NAME.test(hardware) || !NAME.test(channel)) {
      return c.json(error('bad_request', 'hardware and channel are short lowercase names'), 400);
    }
    const release = latestRelease(env.config, { hardware, channel });
    if (!release) return c.json(error('no_release', `Nothing published for ${hardware}/${channel}`), 404);

    // `installed` only decides the one boolean. The phone could work it out
    // itself, but then every client would carry its own version comparison,
    // and "is 0.10.0 newer than 0.9.9" is exactly the sort of thing they
    // would each get wrong differently.
    const installed = c.req.query('installed');
    const known = installed && VERSION.test(installed) ? installed : null;
    return c.json({
      ...releaseJson(env.config, release),
      update_available: known ? compareVersions(release.version, known) > 0 : true,
    });
  });

  // /api/firmware/<hardware>/<version>.bin
  app.get('/:hardware/:image', (c) => {
    const hardware = c.req.param('hardware');
    const m = c.req.param('image').match(/^(\d{1,3}\.\d{1,3}\.\d{1,3})\.bin$/);
    if (!NAME.test(hardware) || !m) return c.notFound();
    const release = findRelease(env.config, hardware, m[1]);
    if (!release) return c.notFound();

    // The image for a version never changes — publishing a different build
    // means publishing a different version — so it can be cached forever.
    const etag = `"${release.sha256}"`;
    if (c.req.header('if-none-match') === etag) return c.body(null, 304);
    return c.body(releaseBytes(env.config, release), 200, {
      'content-type': 'application/octet-stream',
      'content-disposition': `attachment; filename="${release.hardware}-${release.version}.bin"`,
      'cache-control': 'public, max-age=31536000, immutable',
      'x-firmware-sha256': release.sha256,
      etag,
    });
  });

  return app;
}
