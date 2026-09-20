// Host router. One process serves three things:
//
//   apex           (BASE_DOMAIN)          /api/* and /app/*, nothing else
//   <handle>.apex                         that owner's blog, at its own root
//   anything else                         404
//
// The apex root is *not* ours: a landing page lives there, served by whatever
// proxy sits in front, and only the two prefixes in config.ts are forwarded
// here. `/` is kept as a redirect into the app so that running this server on
// its own (pnpm dev, a bare container) still lands somewhere useful.
//
// The blog sub-app receives the resolved profile as its Hono "bindings"
// (c.env.profile): that is the hook Hono offers for per-request data
// handed in by whoever calls fetch(), and it saves every blog route a
// lookup. A reserved label such as www.<apex> bounces to the apex.

import { Hono } from 'hono';
import { API_PREFIX, APP_PREFIX, apexUrl } from './config.js';
import type { Env } from './env.js';
import { RESERVED_HANDLES, handleFromHost } from './lib/handles.js';
import { findProfileByHandle } from './repo/profiles.js';
import { blogRoutes } from './routes/blog.js';
import { cameraRoutes } from './routes/camera.js';
import { firmwareRoutes } from './routes/firmware.js';
import { webRoutes } from './routes/web.js';

export { HANDLE, RESERVED_HANDLES, handleFromHost } from './lib/handles.js';

export function createApp(env: Env): Hono {
  const apex = new Hono();
  apex.route(`${API_PREFIX}/camera`, cameraRoutes(env));
  apex.route(`${API_PREFIX}/firmware`, firmwareRoutes(env));
  apex.route(APP_PREFIX, webRoutes(env));
  apex.get('/', (c) => c.redirect(APP_PREFIX, 302));

  const blog = blogRoutes(env);

  const app = new Hono();
  app.onError((err, c) => {
    console.error(err);
    return c.text('Something broke.', 500);
  });

  app.all('*', (c) => {
    const host = (c.req.header('host') ?? new URL(c.req.url).host).toLowerCase();
    if (host === env.config.baseDomain) return apex.fetch(c.req.raw);
    const handle = handleFromHost(host, env.config.baseDomain);
    if (handle && RESERVED_HANDLES.has(handle)) return c.redirect(apexUrl(env.config), 302);
    const profile = handle ? findProfileByHandle(env.db, handle) : null;
    if (!profile || !profile.email_verified_at) return c.text('No such little camera.', 404);
    return blog.fetch(c.req.raw, { profile });
  });

  return app;
}
