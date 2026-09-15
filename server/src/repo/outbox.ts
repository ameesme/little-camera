import type { Db } from '../db.js';

export interface OutboxRow {
  id: number;
  to_email: string;
  subject: string;
  text_body: string;
  html_body: string;
  photo_id: number | null;
  status: 'pending' | 'sent' | 'failed';
  attempts: number;
  last_error: string | null;
  created_at: number;
  sent_at: number | null;
}

export function insertOutbox(
  db: Db,
  m: { to: string; subject: string; text: string; html: string; photoId: number | null; now: number },
): OutboxRow {
  const r = db
    .prepare(
      `INSERT INTO email_outbox (to_email, subject, text_body, html_body, photo_id, created_at)
       VALUES (?, ?, ?, ?, ?, ?)`,
    )
    .run(m.to, m.subject, m.text, m.html, m.photoId, m.now);
  return findOutbox(db, Number(r.lastInsertRowid))!;
}

export function findOutbox(db: Db, id: number): OutboxRow | null {
  return (db.prepare('SELECT * FROM email_outbox WHERE id = ?').get(id) as OutboxRow | undefined) ?? null;
}

export function pendingOutbox(db: Db, limit = 20): OutboxRow[] {
  return db
    .prepare(`SELECT * FROM email_outbox WHERE status = 'pending' AND attempts < 5 ORDER BY id LIMIT ?`)
    .all(limit) as unknown as OutboxRow[];
}

export function listOutbox(db: Db, limit = 100): OutboxRow[] {
  return db.prepare('SELECT * FROM email_outbox ORDER BY id DESC LIMIT ?').all(limit) as unknown as OutboxRow[];
}

export function markOutboxSent(db: Db, id: number, now: number): void {
  db.prepare(`UPDATE email_outbox SET status = 'sent', sent_at = ?, attempts = attempts + 1 WHERE id = ?`).run(now, id);
}

export function markOutboxFailed(db: Db, id: number, error: string): void {
  // Stays pending for a retry until attempts reaches 5, then failed.
  db.prepare(
    `UPDATE email_outbox
     SET attempts = attempts + 1, last_error = ?,
         status = CASE WHEN attempts + 1 >= 5 THEN 'failed' ELSE 'pending' END
     WHERE id = ?`,
  ).run(error.slice(0, 500), id);
}
