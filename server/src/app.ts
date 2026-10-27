import { Hono } from 'hono';
import { API_PREFIX, APP_PREFIX, apexUrl } from './config.js';
import type { Env } from './env.js';
import { RESERVED_HANDLES, handleFromHost } from './lib/handles.js';
import { findProfileByHandle } from './repo/profiles.js';
import { blogRoutes } from './routes/blog.js';
import { cameraRoutes } from './routes/camera.js';
import { firmwareRoutes } from './routes/firmware.js';
import { ownerRoutes } from './routes/owner.js';

export { HANDLE, RESERVED_HANDLES, handleFromHost } from './lib/handles.js';

/** Routed by Host: the bare domain answers under its two prefixes, `<handle>.BASE_DOMAIN` is that owner's blog. */
export function createApp(env: Env): Hono {
  const owner = new Hono();
  owner.route(`${API_PREFIX}/camera`, cameraRoutes(env));
  owner.route(`${API_PREFIX}/firmware`, firmwareRoutes(env));
  owner.route(APP_PREFIX, ownerRoutes(env));
  // So that a server running on its own — pnpm dev, a bare container — still lands somewhere useful.
  owner.get('/', (c) => c.redirect(APP_PREFIX, 302));

  const blog = blogRoutes(env);

  const app = new Hono();
  app.onError((err, c) => {
    console.error(err);
    return c.text('Something broke.', 500);
  });

  app.all('*', (c) => {
    const host = (c.req.header('host') ?? new URL(c.req.url).host).toLowerCase();
    if (host === env.config.baseDomain) return owner.fetch(c.req.raw);
    const handle = handleFromHost(host, env.config.baseDomain);
    if (handle && RESERVED_HANDLES.has(handle)) return c.redirect(apexUrl(env.config), 302);
    const profile = handle ? findProfileByHandle(env.db, handle) : null;
    if (!profile || !profile.email_verified_at) return c.text('No such little camera.', 404);
    // The blog sub-app takes the resolved profile as Hono bindings, which saves every blog route a lookup.
    return blog.fetch(c.req.raw, { profile });
  });

  return app;
}
