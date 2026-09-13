// Host router. One process serves three things:
//
//   apex           (BASE_DOMAIN)          registration, /me, the camera API
//   <handle>.apex                         that owner's blog
//   anything else                         404

import { Hono } from 'hono';
import type { Env } from './env.js';
import { cameraRoutes } from './routes/camera.js';

export const HANDLE = /^[a-z0-9][a-z0-9-]{1,30}$/;
export const RESERVED_HANDLES = new Set(['www', 'api', 'dev', 'mail', 'admin', 'app']);

/** `mees.example.com:3000` + `example.com:3000` → `mees`; null when the host is not one level under the apex. */
export function handleFromHost(host: string, baseDomain: string): string | null {
  if (!host.endsWith('.' + baseDomain)) return null;
  const handle = host.slice(0, -(baseDomain.length + 1));
  return HANDLE.test(handle) ? handle : null;
}

export function createApp(env: Env): Hono {
  const apex = new Hono();
  apex.route('/api/camera', cameraRoutes(env));
  apex.get('/', (c) => c.text('little camera'));

  const app = new Hono();
  app.onError((err, c) => {
    console.error(err);
    return c.text('Something broke.', 500);
  });

  app.all('*', (c) => {
    const host = (c.req.header('host') ?? new URL(c.req.url).host).toLowerCase();
    if (host === env.config.baseDomain) return apex.fetch(c.req.raw);
    return c.text('No such little camera.', 404);
  });

  return app;
}
