// Email templates. Plain text is the primary version; the HTML version is
// the same words in the blog's ink-on-paper look. No images except the
// inline photo in the newsletter (cid:photo), no colour anywhere.

export interface Mail {
  subject: string;
  text: string;
  html: string;
}

const esc = (s: string) =>
  s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');

/** Shared HTML wrapper: 440px column, 1px rules, uppercase tracked heading, black button. */
function shell(title: string, paragraphs: string[], button?: { label: string; url: string }, photoCid?: string): string {
  const body = paragraphs.map((p) => `<p style="margin:0 0 12px;font-size:15px;line-height:1.45">${p}</p>`).join('');
  const btn = button
    ? `<p style="margin:18px 0 6px"><a href="${esc(button.url)}" style="display:inline-block;background:#000;color:#fff;text-decoration:none;font-size:11px;font-weight:700;letter-spacing:.1em;text-transform:uppercase;padding:10px 14px;border:1px solid #000">${esc(button.label)}</a></p>
       <p style="margin:0 0 12px;font-size:12px;word-break:break-all"><a href="${esc(button.url)}" style="color:#000">${esc(button.url)}</a></p>`
    : '';
  const img = photoCid
    ? `<img src="cid:${photoCid}" width="320" height="240" alt="The newest picture" style="display:block;width:100%;max-width:412px;height:auto;border-top:1px solid #000;border-bottom:1px solid #000;image-rendering:pixelated;margin:0 0 14px">`
    : '';
  return `<!doctype html><html><head><meta charset="utf-8"><meta name="color-scheme" content="light"><title>${esc(title)}</title></head>
<body style="margin:0;padding:0;background:#ffffff;color:#000000;font-family:'Helvetica Neue',Helvetica,Arial,sans-serif">
<table role="presentation" width="100%" cellpadding="0" cellspacing="0" style="background:#ffffff"><tr><td align="center" style="padding:24px 14px">
<table role="presentation" width="440" cellpadding="0" cellspacing="0" style="max-width:440px;width:100%;border:1px solid #000;background:#fff;color:#000">
<tr><td style="padding:12px 14px;border-bottom:1px solid #000;font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase">${esc(title)}</td></tr>
${img ? `<tr><td>${img}</td></tr>` : ''}
<tr><td style="padding:14px">${body}${btn}</td></tr>
</table>
<p style="font-size:10px;letter-spacing:.09em;text-transform:uppercase;margin:14px 0 0">Little camera</p>
</td></tr></table></body></html>`;
}

export function verifyEmailMail(p: { name: string; url: string }): Mail {
  return {
    subject: 'Confirm your email for your little camera',
    text: `Hi ${p.name},\n\nConfirm your email to finish setting up your little camera:\n\n${p.url}\n\nThe link works for 24 hours.\n`,
    html: shell(
      'Confirm your email',
      [`Hi ${esc(p.name)},`, 'Confirm your email to finish setting up your little camera. The link works for 24 hours.'],
      { label: 'Confirm email', url: p.url },
    ),
  };
}

export function loginLinkMail(p: { name: string; url: string }): Mail {
  return {
    subject: 'Your sign-in link',
    text: `Hi ${p.name},\n\nSign in to your little camera:\n\n${p.url}\n\nThe link works for one hour.\n`,
    html: shell('Sign in', [`Hi ${esc(p.name)},`, 'Sign in to your little camera. The link works for one hour.'], {
      label: 'Sign in',
      url: p.url,
    }),
  };
}

export function cameraLinkedMail(p: { name: string; blogUrl: string }): Mail {
  return {
    subject: 'Your camera is linked',
    text: `Hi ${p.name},\n\nYour camera is linked. Every picture you send now ends up on\n\n${p.blogUrl}\n\nOpen the app to add subscribers or to take your profile picture.\n`,
    html: shell(
      'Your camera is linked',
      [`Hi ${esc(p.name)},`, 'Every picture you send now ends up on your blog. Open the app to add subscribers or to take your profile picture.'],
      { label: 'Open your blog', url: p.blogUrl },
    ),
  };
}

export function newSubscriberMail(p: { ownerName: string; email: string; meUrl: string }): Mail {
  return {
    subject: `${p.email} wants to follow your little camera`,
    text: `Hi ${p.ownerName},\n\n${p.email} asked to follow your little camera. Approve them in the app, or here:\n\n${p.meUrl}\n`,
    html: shell(
      'Someone wants to follow',
      [`Hi ${esc(p.ownerName)},`, `<b>${esc(p.email)}</b> asked to follow your little camera. Approve them in the app, or on the web.`],
      { label: 'Manage subscribers', url: p.meUrl },
    ),
  };
}

export function welcomeMail(p: { ownerName: string; url: string }): Mail {
  return {
    subject: `You can now see ${p.ownerName}'s little camera`,
    text: `You are in. ${p.ownerName}'s pictures are here:\n\n${p.url}\n\nThe link works for 48 hours; every new batch of pictures brings a fresh one.\n`,
    html: shell(
      `${p.ownerName}'s little camera`,
      ['You are in.', 'The link works for 48 hours; every new batch of pictures brings a fresh one.'],
      { label: 'See the pictures', url: p.url },
    ),
  };
}

export function newsletterMail(p: { ownerName: string; count: number; url: string }): Mail {
  const what = p.count === 1 ? 'a picture' : `${p.count} new pictures`;
  const subject = `${p.ownerName} took ${what}`;
  return {
    subject,
    text: `${subject}.\n\n${p.url}\n\nThe link works for 48 hours.\n`,
    html: shell(subject, ['The link works for 48 hours.'], { label: 'See the pictures', url: p.url }, 'photo'),
  };
}

export function freshLinkMail(p: { ownerName: string; url: string }): Mail {
  return {
    subject: `A fresh link to ${p.ownerName}'s little camera`,
    text: `Here is a fresh link, good for 48 hours:\n\n${p.url}\n`,
    html: shell(`${p.ownerName}'s little camera`, ['Here is a fresh link, good for 48 hours.'], {
      label: 'See the pictures',
      url: p.url,
    }),
  };
}
