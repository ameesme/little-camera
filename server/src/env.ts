// What every route and job needs, built once in index.ts and fresh per test.
import type { Config } from './config.js';
import type { Db } from './db.js';

export interface Env {
  db: Db;
  config: Config;
  /** Unix seconds. Injected so tests can move time forward. */
  now: () => number;
}
