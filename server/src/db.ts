// SQLite through Node's built-in `node:sqlite`. No ORM: the SQL lives in
// src/repo/*.ts where a reader can see exactly what runs.
//
// node:sqlite is still flagged experimental in Node 22 (it prints one
// warning at startup) but it is synchronous, has no native build step and
// ships with the runtime, which beats compiling better-sqlite3 on a VPS.

import { DatabaseSync } from 'node:sqlite';
import { mkdirSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

export type Db = DatabaseSync;

const migrationsDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'migrations');

/** Open (or create) the database and bring the schema up to date. `:memory:` for tests. */
export function openDb(path: string): Db {
  if (path !== ':memory:') mkdirSync(dirname(path), { recursive: true });
  const db = new DatabaseSync(path);
  // WAL lets the newsletter job read while a photo upload writes. Not
  // applicable to in-memory databases, where the pragma is a no-op.
  db.exec('PRAGMA journal_mode = WAL');
  db.exec('PRAGMA foreign_keys = ON');
  db.exec('PRAGMA busy_timeout = 5000');
  migrate(db);
  return db;
}

/** Apply every migrations/NNN_*.sql that is not yet in schema_migrations, in name order. */
export function migrate(db: Db): void {
  db.exec(`CREATE TABLE IF NOT EXISTS schema_migrations (
    name TEXT PRIMARY KEY,
    applied_at INTEGER NOT NULL
  )`);
  const applied = new Set(
    (db.prepare('SELECT name FROM schema_migrations').all() as { name: string }[]).map((r) => r.name),
  );
  const files = readdirSync(migrationsDir)
    .filter((f) => f.endsWith('.sql'))
    .sort();
  for (const file of files) {
    if (applied.has(file)) continue;
    const sql = readFileSync(join(migrationsDir, file), 'utf8');
    db.exec('BEGIN');
    try {
      db.exec(sql);
      db.prepare('INSERT INTO schema_migrations (name, applied_at) VALUES (?, ?)').run(file, now());
      db.exec('COMMIT');
    } catch (err) {
      db.exec('ROLLBACK');
      throw new Error(`migration ${file} failed: ${(err as Error).message}`);
    }
  }
}

/** Unix seconds. Every timestamp in the database uses this. */
export function now(): number {
  return Math.floor(Date.now() / 1000);
}

/** Run `fn` inside a transaction; rolls back on throw. */
export function transaction<T>(db: Db, fn: () => T): T {
  db.exec('BEGIN IMMEDIATE');
  try {
    const result = fn();
    db.exec('COMMIT');
    return result;
  } catch (err) {
    db.exec('ROLLBACK');
    throw err;
  }
}
