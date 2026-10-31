// Every message the server sends. Plain text is the primary version; the HTML is the same words in ink on paper.

import { cameraArt } from './art.js';

export interface Mail {
  subject: string;
  text: string;
  html: string;
}

const esc = (s: string) =>
  s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

interface Shell {
  title: string;
  paragraphs: string[];
  button?: { label: string; url: string };
  /** The newest picture, attached by lib/email.ts under this cid. */
  photoCid?: string;
  /** The camera itself, drawn in table cells above the words. */
  art?: boolean;
  footer?: string;
}

/** Shared HTML wrapper: 440px column, 1px rules, uppercase tracked heading, black button. */
function shell(s: Shell): string {
  const body = s.paragraphs.map((p) => `<p style="margin:0 0 12px;font-size:15px;line-height:1.45">${p}</p>`).join('');
  // The raw URL under the button is for whoever cannot press it, so it needs air above it
  // rather than looking like the button's own caption.
  const btn = s.button
    ? `<p style="margin:18px 0 20px"><a href="${esc(s.button.url)}" style="display:inline-block;background:#000;color:#fff;text-decoration:none;font-size:11px;font-weight:700;letter-spacing:.1em;text-transform:uppercase;padding:10px 14px;border:1px solid #000">${esc(s.button.label)}</a></p>
       <p style="margin:0 0 12px;font-size:12px;word-break:break-all"><a href="${esc(s.button.url)}" style="color:#000">${esc(s.button.url)}</a></p>`
    : '';
  // Full bleed: a picture that stops short of the column reads as a mistake.
  const img = s.photoCid
    ? `<img src="cid:${s.photoCid}" width="438" alt="The newest picture" style="display:block;width:100%;height:auto;border-bottom:1px solid #000;image-rendering:pixelated">`
    : '';
  const art = s.art ? `<tr><td style="padding:22px 14px 4px">${cameraArt()}</td></tr>` : '';
  const footer = s.footer ? `<p style="font-size:11px;margin:10px 0 0">${s.footer}</p>` : '';
  return `<!doctype html><html><head><meta charset="utf-8"><meta name="color-scheme" content="light"><title>${esc(s.title)}</title></head>
<body style="margin:0;padding:0;background:#ffffff;color:#000000;font-family:'Helvetica Neue',Helvetica,Arial,sans-serif">
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background:#ffffff"><tr><td align="center" style="padding:24px 14px">
<table role="presentation" width="440" cellpadding="0" cellspacing="0" style="max-width:440px;width:100%;border:1px solid #000;background:#fff;color:#000">
<tr><td style="padding:12px 14px;border-bottom:1px solid #000;font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase">${esc(s.title)}</td></tr>
${img ? `<tr><td>${img}</td></tr>` : ''}
${art}
<tr><td style="padding:14px">${body}${btn}</td></tr>
</table>
<p style="font-size:10px;letter-spacing:.09em;text-transform:uppercase;margin:14px 0 0">Little camera</p>
${footer}
</td></tr></table></body></html>`;
}

/** Sent before the blog exists, so it greets nobody by name and must not imply one is waiting. */
export function confirmAddressMail(p: { url: string }): Mail {
  return {
    subject: 'Confirm your email for your little camera',
    text: `Hi,\n\nConfirm this address and your little camera can start a little blog on it:\n\n${p.url}\n\nThe link works for an hour. If you did not ask for this, nothing has been made and you can ignore it.\n`,
    html: shell({
      title: 'Confirm your email',
      paragraphs: [
        'Confirm this address and your little camera can start a little blog on it. The link works for an hour.',
        'If you did not ask for this, nothing has been made and you can ignore this message.',
      ],
      button: { label: 'Confirm email', url: p.url },
    }),
  };
}

export function cameraLinkedMail(p: { name: string; blogUrl: string }): Mail {
  return {
    subject: 'Your camera is linked',
    text: `Hi ${p.name},\n\nYour camera is linked. Every picture you send now ends up on\n\n${p.blogUrl}\n\nOpen the app to add subscribers or to take your profile picture.\n`,
    html: shell({
      title: 'Your camera is linked',
      art: true,
      paragraphs: [`Hi ${esc(p.name)},`, 'Every picture you send now ends up on your blog. Open the app to add subscribers or to take your profile picture.'],
      button: { label: 'Open your blog', url: p.blogUrl },
    }),
  };
}

export function deviceClaimMail(p: { name: string; blogName: string; url: string }): Mail {
  return {
    subject: 'New phone?',
    text: `Hi ${p.name},\n\nSome phone with a little camera wants to access ${p.blogName}. If this was you, you can let it in here:\n\n${p.url}\n\nThe link works for an hour.\n\nNot you? Just ignore this.\n`,
    html: shell({
      title: 'New phone?',
      paragraphs: [
        `Hi ${esc(p.name)},`,
        `Some phone with a little camera wants to access <b>${esc(p.blogName)}</b>. If this was you, you can let it in below. The link works for an hour.`,
        'Not you? Just ignore this.',
      ],
      button: { label: 'Let this phone in', url: p.url },
    }),
  };
}

export function newSubscriberMail(p: { ownerName: string; email: string }): Mail {
  return {
    subject: `${p.email} wants to follow your little camera`,
    text: `Hi ${p.ownerName},\n\n${p.email} asked to follow your little camera. Open the app to let them in.\n`,
    html: shell({
      title: 'Someone wants to follow',
      paragraphs: [`Hi ${esc(p.ownerName)},`, `<b>${esc(p.email)}</b> asked to follow your little camera. Open the app to let them in.`],
    }),
  };
}

export function welcomeMail(p: { ownerName: string; url: string; unsubscribeUrl: string }): Mail {
  const title = `You can now see ${p.ownerName}'s little camera`;
  return {
    subject: title,
    text: `${title}.\n\n${p.url}\n\nThis link is valid for 48 hours, you get a new one with every new batch of photos, or request a fresh one at any time.\n\nStop getting these: ${p.unsubscribeUrl}\n`,
    html: shell({
      title,
      paragraphs: [
        'This link is valid for 48 hours, you get a new one with every new batch of photos, or request a fresh one at any time.',
      ],
      button: { label: 'See the pictures', url: p.url },
      footer: unsubscribeFooter(p.unsubscribeUrl),
    }),
  };
}

export function newsletterMail(p: { ownerName: string; count: number; url: string; unsubscribeUrl: string }): Mail {
  const what = p.count === 1 ? 'a picture' : `${p.count} new pictures`;
  const subject = `${p.ownerName} took ${what}`;
  return {
    subject,
    text: `${subject}.\n\n${p.url}\n\nThe link works for 48 hours.\n\nStop getting these: ${p.unsubscribeUrl}\n`,
    html: shell({
      title: subject,
      photoCid: 'photo',
      paragraphs: ['The link works for 48 hours.'],
      button: { label: 'See the pictures', url: p.url },
      footer: unsubscribeFooter(p.unsubscribeUrl),
    }),
  };
}

export function freshLinkMail(p: { ownerName: string; url: string; unsubscribeUrl: string }): Mail {
  return {
    subject: `A fresh link to ${p.ownerName}'s little camera`,
    text: `Here is a fresh link, good for 48 hours:\n\n${p.url}\n\nStop getting these: ${p.unsubscribeUrl}\n`,
    html: shell({
      title: `${p.ownerName}'s little camera`,
      paragraphs: ['Here is a fresh link, good for 48 hours.'],
      button: { label: 'See the pictures', url: p.url },
      footer: unsubscribeFooter(p.unsubscribeUrl),
    }),
  };
}

/** Every mail a subscriber gets carries the way out of them. */
function unsubscribeFooter(url: string): string {
  return `<a href="${esc(url)}" style="color:#000">Stop getting these</a>`;
}
