// The bag of things every route and job needs. Built once in index.ts,
// built fresh (with an in-memory database and a fake clock) in tests.

import type { Config } from './config.js';
import type { Db } from './db.js';

export interface Env {
  db: Db;
  config: Config;
  /** Unix seconds. Injected so tests can move time forward. */
  now: () => number;
}
