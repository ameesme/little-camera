// Mail goes through a table so sending is one transactional insert; a drainer does the SMTP talking.

import nodemailer, { type Transporter } from 'nodemailer';
import { photoPng } from './photo-png.js';
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
    // Mail clients that know APNG animate a motion photo; the rest show its first frame.
    return [{ filename: `${photo.public_id}.png`, content: Buffer.from(photoPng(env, photo)), cid: 'photo', contentType: 'image/png' }];
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
