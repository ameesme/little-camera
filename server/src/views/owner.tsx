import type { FC } from 'hono/jsx';
import { APP_PREFIX } from '../config.js';
import { Simple, Stage } from './layout.js';

/** The two pages an emailed link opens live under APP_PREFIX, since `/` on this domain belongs to the landing page. */
export const u = (path = '') => `${APP_PREFIX}${path}`;

export const AddressConfirmed: FC = () => (
  <Stage title="Email confirmed" heading="Email confirmed">
    <p>Go back to the app to name your little blog.</p>
  </Stage>
);

export const ExpiredLink: FC = () => (
  <Stage title="This link has expired" heading="This link has expired">
    <p>Ask the app for a new one.</p>
  </Stage>
);

export const ClaimPrompt: FC<{ blogName: string; shortCode: string; token: string }> = ({ blogName, shortCode, token }) => (
  <Stage
    title="Let this phone in?"
    heading="Let this phone in?"
    actions={
      <form method="post" action={u('/claim')}>
        <input type="hidden" name="t" value={token} />
        <button class="big" type="submit">Let it in</button>
      </form>
    }
  >
    <p>
      A phone holding camera <code>{shortCode}</code> wants to manage <b>{blogName}</b>: your pictures, your comments and
      who is on your list.
    </p>
    <p>Only do this if it is your phone, and if you just asked it to.</p>
  </Stage>
);

export const ClaimDone: FC<{ blogName: string }> = ({ blogName }) => (
  <Stage title="Phone let in" heading="Phone let in">
    <p>That phone can manage <b>{blogName}</b> now. Go back to the app.</p>
  </Stage>
);

export const Mailbox: FC<{ rows: { id: number; subject: string; to_email: string; status: string; when: string }[] }> = ({ rows }) => (
  <Simple title="Dev mailbox" heading="Dev mailbox">
    <p>
      No SMTP server is configured, so nothing here was actually sent. Every message the server
      would have posted lands in this list instead; open one to read it and to follow the link it
      carries. This page does not exist in production.
    </p>
    {rows.length === 0 ? <p>Nothing sent yet.</p> : null}
    <ul class="list mail">
      {rows.map((m) => (
        <li>
          <span>
            <a href={u(`/dev/mailbox/${m.id}`)}>{m.subject}</a>
            <br />
            <span class="tag">to {m.to_email} · {m.status} · {m.when}</span>
          </span>
        </li>
      ))}
    </ul>
  </Simple>
);

export const MailView: FC<{ subject: string; to: string; status: string; text: unknown; html: string }> = ({ subject, to, status, text, html }) => (
  <Simple title={subject} heading={subject}>
    <div class="mail">
      <p>To {to} · {status} · <a href={u('/dev/mailbox')}>back</a></p>
      <pre>{text}</pre>
      <iframe srcdoc={html} title="HTML version"></iframe>
    </div>
  </Simple>
);
