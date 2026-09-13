import { serve } from '@hono/node-server';
import { join } from 'node:path';
import { createApp } from './app.js';
import { loadConfig } from './config.js';
import { now, openDb } from './db.js';
import type { Env } from './env.js';
import { startEmailDrainer } from './lib/email.js';

const config = loadConfig();
const env: Env = { db: openDb(join(config.dataDir, 'little-camera.sqlite')), config, now };

startEmailDrainer(env);

serve({ fetch: createApp(env).fetch, port: config.port }, (info) => {
  console.log(`little camera server on ${config.publicScheme}://${config.baseDomain} (port ${info.port})`);
  if (!config.smtpUrl) console.log(`no SMTP_URL: mail lands in ${config.publicScheme}://${config.baseDomain}/dev/mailbox`);
});
