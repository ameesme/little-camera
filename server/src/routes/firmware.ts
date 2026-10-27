import { Hono } from 'hono';
import {
  DEFAULT_CHANNEL,
  DEFAULT_HARDWARE,
  VERSION,
  findRelease,
  firmwareState,
  releaseBytes,
} from '../core/firmware.js';
import type { Env } from '../env.js';

const NAME = /^[a-z0-9][a-z0-9_-]{0,31}$/;
const IMAGE = /^(\d{1,3}\.\d{1,3}\.\d{1,3})\.bin$/;
const error = (code: string, message: string) => ({ error: code, message });

// Unauthenticated on purpose: what keeps a bad image off a camera is the SHA-256 it checks itself (§3.7), not who may download one.
export function firmwareRoutes(env: Env): Hono {
  const app = new Hono();

  app.get('/latest', (c) => {
    const hardware = c.req.query('hardware') ?? DEFAULT_HARDWARE;
    const channel = c.req.query('channel') ?? DEFAULT_CHANNEL;
    if (!NAME.test(hardware) || !NAME.test(channel)) {
      return c.json(error('bad_request', 'hardware and channel are short lowercase names'), 400);
    }
    const installed = c.req.query('installed');
    const state = firmwareState(env.config, installed && VERSION.test(installed) ? installed : null, { hardware, channel });
    if (!state.latest) return c.json(error('no_release', `Nothing published for ${hardware}/${channel}`), 404);
    return c.json({ ...state.latest, update_available: state.installed ? state.update_available : true });
  });

  app.get('/:hardware/:image', (c) => {
    const hardware = c.req.param('hardware');
    const version = c.req.param('image').match(IMAGE);
    if (!NAME.test(hardware) || !version) return c.notFound();
    const release = findRelease(env.config, hardware, version[1]);
    if (!release) return c.notFound();

    // A given version's image never changes, since a different build means a different version.
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
