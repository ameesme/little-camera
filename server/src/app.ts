// Host router. One process serves three things:
//
//   apex           (BASE_DOMAIN)          registration, /me, the camera API
//   <handle>.apex                         that owner's blog
//   anything else                         404
//
// The blog sub-app receives the resolved profile as its Hono "bindings"
// (c.env.profile): that is the hook Hono offers for per-request data
// handed in by whoever calls fetch(), and it saves every blog route a
// lookup. A reserved label such as www.<apex> bounces to the apex.

import { Hono } from 'hono';
import { apexUrl } from './config.js';
import type { Env } from './env.js';
import { RESERVED_HANDLES, handleFromHost } from './lib/handles.js';
import { findProfileByHandle } from './repo/profiles.js';
import { blogRoutes } from './routes/blog.js';
import { cameraRoutes } from './routes/camera.js';
import { webRoutes } from './routes/web.js';

export { HANDLE, RESERVED_HANDLES, handleFromHost } from './lib/handles.js';

export function createApp(env: Env): Hono {
  const apex = new Hono();
  apex.route('/api/camera', cameraRoutes(env));
  apex.route('/', webRoutes(env));

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
