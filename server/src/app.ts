import { Hono } from 'hono';
import { API_PREFIX, APP_PREFIX, apexUrl } from './config.js';
import type { Env } from './env.js';
import { RESERVED_HANDLES, handleFromHost } from './lib/handles.js';
import { findBlogByHandle } from './repo/blogs.js';
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

  const blogs = blogRoutes(env);

  const app = new Hono();
  app.onError((err, c) => {
    console.error(err);
    return c.text('Something broke.', 500);
  });

  app.get('/caddy/ask', (c) => {
    const domain = c.req.query('domain') ?? '';
    if (domain === env.config.baseDomain) return c.text('ok');
    const handle = handleFromHost(domain, env.config.baseDomain);
    if (!handle || RESERVED_HANDLES.has(handle)) return c.text('no', 404);
    const blog = findBlogByHandle(env.db, handle);
    return blog?.email_verified_at ? c.text('ok') : c.text('no', 404);
  });

  app.all('*', (c) => {
    const host = (c.req.header('host') ?? new URL(c.req.url).host).toLowerCase();
    if (host === env.config.baseDomain) return owner.fetch(c.req.raw);
    const handle = handleFromHost(host, env.config.baseDomain);
    if (handle && RESERVED_HANDLES.has(handle)) return c.redirect(apexUrl(env.config), 302);
    const blog = handle ? findBlogByHandle(env.db, handle) : null;
    if (!blog || !blog.email_verified_at) return c.text('No such little camera.', 404);
    // The sub-app takes the resolved blog as Hono bindings, which saves every blog route a lookup.
    return blogs.fetch(c.req.raw, { blog });
  });

  return app;
}
