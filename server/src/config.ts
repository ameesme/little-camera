// Everything the server reads from the environment, in one place.
// Defaults are for `pnpm dev` on a laptop; production sets them all.

export interface Config {
  port: number;
  /** Apex host including the port when non-standard, e.g. `localhost:3000` or `littlecamera.example`. */
  baseDomain: string;
  /** `http` in dev, `https` behind Caddy. */
  publicScheme: 'http' | 'https';
  /** Where the SQLite file and the photo files live. */
  dataDir: string;
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
  return {
    port: Number(env.PORT ?? 3000),
    baseDomain: (env.BASE_DOMAIN ?? 'localhost:3000').toLowerCase(),
    publicScheme: scheme,
    dataDir: env.DATA_DIR ?? './data',
    sessionSecret,
    smtpUrl: env.SMTP_URL || null,
    mailFrom: env.MAIL_FROM ?? 'Little Camera <camera@localhost>',
    nodeEnv,
    displayTz: env.DISPLAY_TZ ?? 'Europe/Amsterdam',
  };
}

/** `https://mees.example.com` — the public URL of a blog. */
export function blogUrl(config: Config, handle: string): string {
  return `${config.publicScheme}://${handle}.${config.baseDomain}`;
}

/** `https://example.com` — the public URL of the apex (registration, /me, API). */
export function apexUrl(config: Config): string {
  return `${config.publicScheme}://${config.baseDomain}`;
}

/** Host name without port, for the cookie Domain attribute. */
export function apexHost(config: Config): string {
  return config.baseDomain.split(':')[0];
}
