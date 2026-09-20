// Everything the server reads from the environment, in one place.
// Defaults are for `pnpm dev` on a laptop; production sets them all.

/**
 * The two prefixes this server owns on the apex: `/api` for machines,
 * `/app` for pages a person opens. Everything else on the apex host — `/`
 * above all — belongs to the landing page, which is a separate thing behind
 * the same proxy, so those two are all it has to forward here. Blogs are not
 * affected: each one owns the root of its own subdomain.
 */
export const API_PREFIX = '/api';
export const APP_PREFIX = '/app';

export interface Config {
  port: number;
  /** Apex host including the port when non-standard, e.g. `localhost:3000` or `lttl.cam`. */
  baseDomain: string;
  /** `http` in dev, `https` behind Caddy. */
  publicScheme: 'http' | 'https';
  /** Where the SQLite file and the photo files live. */
  dataDir: string;
  /** Firmware images and their manifest (protocol §4.7). Defaults to `<dataDir>/firmware`. */
  firmwareDir: string;
  /** Signs the owner and subscriber cookies. */
  sessionSecret: string;
  /** `smtp://user:pass@host:587`. Unset = every mail is only visible in /dev/mailbox. */
  smtpUrl: string | null;
  mailFrom: string;
  nodeEnv: string;
  /** IANA zone used to print dates on the blog. One zone for the whole server keeps it simple. */
  displayTz: string;
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): Config {
  const nodeEnv = env.NODE_ENV ?? 'development';
  const sessionSecret = env.SESSION_SECRET ?? (nodeEnv === 'production' ? '' : 'dev-secret-change-me');
  if (!sessionSecret) throw new Error('SESSION_SECRET is required in production');
  const scheme = env.PUBLIC_SCHEME ?? 'http';
  if (scheme !== 'http' && scheme !== 'https') throw new Error('PUBLIC_SCHEME must be http or https');
  const dataDir = env.DATA_DIR ?? './data';
  return {
    port: Number(env.PORT ?? 3000),
    baseDomain: (env.BASE_DOMAIN ?? 'localhost:3000').toLowerCase(),
    publicScheme: scheme,
    dataDir,
    firmwareDir: env.FIRMWARE_DIR ?? `${dataDir}/firmware`,
    sessionSecret,
    smtpUrl: env.SMTP_URL || null,
    mailFrom: env.MAIL_FROM ?? 'Little Camera <camera@localhost>',
    nodeEnv,
    displayTz: env.DISPLAY_TZ ?? 'Europe/Amsterdam',
  };
}

/** `https://mees.lttl.cam` — the public URL of a blog. */
export function blogUrl(config: Config, handle: string): string {
  return `${config.publicScheme}://${handle}.${config.baseDomain}`;
}

/** `https://lttl.cam` — the apex root, which the landing page owns. */
export function apexUrl(config: Config): string {
  return `${config.publicScheme}://${config.baseDomain}`;
}

/** `https://lttl.cam/app/me` — a page of this server. Every emailed link goes through here. */
export function appUrl(config: Config, path = ''): string {
  return `${apexUrl(config)}${APP_PREFIX}${path}`;
}

/** `https://lttl.cam/api/firmware/...` — an API URL to hand to a client. */
export function apiUrl(config: Config, path = ''): string {
  return `${apexUrl(config)}${API_PREFIX}${path}`;
}

/** Host name without port, for the cookie Domain attribute. */
export function apexHost(config: Config): string {
  return config.baseDomain.split(':')[0];
}
