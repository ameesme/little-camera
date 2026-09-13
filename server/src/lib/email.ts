// Outgoing mail goes through a table, not straight to SMTP. Request
// handlers only insert a row (fast, transactional with the rest of the
// change); a drainer sends pending rows every 10 s. Without SMTP_URL the
// drainer just marks rows sent, and /dev/mailbox shows them.

import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import nodemailer, { type Transporter } from 'nodemailer';
import { parsePbm, pbmToPng } from '@little-camera/pbm';
import type { Env } from '../env.js';
import type { Mail } from '../emails/index.js';
import { findPhotoById } from '../repo/photos.js';
import { insertOutbox, markOutboxFailed, markOutboxSent, pendingOutbox, type OutboxRow } from '../repo/outbox.js';

export function queueEmail(env: Env, to: string, mail: Mail, photoId: number | null = null): OutboxRow {
  return insertOutbox(env.db, { to, subject: mail.subject, text: mail.text, html: mail.html, photoId, now: env.now() });
}

let transporter: Transporter | null = null;

function getTransporter(env: Env): Transporter | null {
  if (!env.config.smtpUrl) return null;
  if (!transporter) transporter = nodemailer.createTransport(env.config.smtpUrl);
  return transporter;
}

/** Render the inline PNG for a newsletter row, if it has one and the file still exists. */
function inlinePhoto(env: Env, photoId: number | null) {
  if (photoId === null) return [];
  const photo = findPhotoById(env.db, photoId);
  if (!photo) return [];
  try {
    const pbm = parsePbm(new Uint8Array(readFileSync(join(env.config.dataDir, photo.path))));
    return [{ filename: `${photo.public_id}.png`, content: Buffer.from(pbmToPng(pbm)), cid: 'photo', contentType: 'image/png' }];
  } catch {
    return [];
  }
}

/** Send everything pending. Safe to call from a timer and from tests. */
export async function drainOutbox(env: Env): Promise<number> {
  const transport = getTransporter(env);
  let sent = 0;
  for (const row of pendingOutbox(env.db)) {
    try {
      if (transport) {
        await transport.sendMail({
          from: env.config.mailFrom,
          to: row.to_email,
          subject: row.subject,
          text: row.text_body,
          html: row.html_body,
          attachments: inlinePhoto(env, row.photo_id),
        });
      }
      markOutboxSent(env.db, row.id, env.now());
      sent++;
    } catch (err) {
      markOutboxFailed(env.db, row.id, (err as Error).message);
      console.error(`[email] #${row.id} to ${row.to_email} failed: ${(err as Error).message}`);
    }
  }
  return sent;
}

export function startEmailDrainer(env: Env, intervalMs = 10_000): () => void {
  const timer = setInterval(() => void drainOutbox(env), intervalMs);
  timer.unref();
  return () => clearInterval(timer);
}
