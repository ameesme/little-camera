import type { FC } from 'hono/jsx';
import { raw } from 'hono/html';
import { APP_PREFIX } from '../config.js';
import type { SubscriberRow } from '../repo/subscribers.js';
import { Page, Simple } from './layout.js';

/** Owner pages live under APP_PREFIX, since `/` on this domain belongs to the landing page. */
export const u = (path = '') => `${APP_PREFIX}${path}`;

export const Landing: FC = () => (
  <Simple title="Little camera" heading="Little camera">
    <p>
      A small black-and-white camera that posts to its own tiny blog. Register, photograph the code on your phone with the
      camera, and every picture you send ends up on your page for the people you let in.
    </p>
    <p>
      <a class="btn" href={u('/register')}>Register</a>{' '}
      <a class="btn" href={u('/login')}>Sign in</a>
    </p>
  </Simple>
);

export const RegisterForm: FC<{ baseDomain: string; values?: Record<string, string>; error?: string }> = ({ baseDomain, values, error }) => (
  <Simple title="Register" heading="Register your little camera">
    {error ? <div class="err">{error}</div> : null}
    <form method="post" action={u('/register')}>
      <label class="field">
        <span>Name</span>
        <input name="name" value={values?.name ?? ''} required maxlength={60} autocomplete="name" />
      </label>
      <label class="field">
        <span>Email</span>
        <input name="email" type="email" value={values?.email ?? ''} required autocomplete="email" />
      </label>
      <label class="field">
        <span>Handle (your blog will be handle.{baseDomain})</span>
        <input name="handle" value={values?.handle ?? ''} required pattern="[a-z0-9][a-z0-9-]{1,30}" autocapitalize="off" autocomplete="off" />
      </label>
      <button class="btn" type="submit">Register</button>
    </form>
    <p style="margin-top:18px">
      Already registered? <a href={u('/login')}>Sign in</a>.
    </p>
  </Simple>
);

export const LoginForm: FC = () => (
  <Simple title="Sign in" heading="Sign in">
    <form method="post" action={u('/login')} class="row">
      <label class="field">
        <span>Email</span>
        <input name="email" type="email" required autocomplete="email" />
      </label>
      <button class="btn" type="submit">Send link</button>
    </form>
  </Simple>
);

export const CheckEmail: FC<{ dev: boolean }> = ({ dev }) => (
  <Simple title="Check your email" heading="Check your email">
    <p>We sent you a link. Open it on your phone: that is where the next step happens.</p>
    {dev ? <p>Development: <a href={u('/dev/mailbox')}>open the dev mailbox</a>.</p> : null}
  </Simple>
);

export const ExpiredLink: FC = () => (
  <Simple title="This link has expired" heading="This link has expired">
    <p><a href={u('/login')}>Ask for a new one</a>.</p>
  </Simple>
);

export const ClaimPrompt: FC<{ blogName: string; shortCode: string; token: string }> = ({ blogName, shortCode, token }) => (
  <Simple title="Let this phone in?" heading="Let this phone in?">
    <p>
      A phone holding camera <code>{shortCode}</code> wants to manage <b>{blogName}</b>: your pictures, your comments and
      who is on your list.
    </p>
    <p>Only do this if it is your phone, and if you just asked it to.</p>
    <form method="post" action={u('/claim')}>
      <input type="hidden" name="t" value={token} />
      <button class="btn" type="submit">Let it in</button>
    </form>
  </Simple>
);

export const ClaimDone: FC<{ blogName: string }> = ({ blogName }) => (
  <Simple title="Phone let in" heading="Phone let in">
    <p>That phone can manage <b>{blogName}</b> now. Go back to the app.</p>
  </Simple>
);

/** Full-viewport white with the QR as large as the screen allows: the camera has to be able to read it. */
export const VerificationPage: FC<{ code: string; svg: string; email: string; apexUrl: string; blogUrl: string }> = ({ code, svg, email, apexUrl, blogUrl }) => (
  <Page title="This is my little camera" wrap={false}>
    <div class="verify" id="verify">
      <p class="stmt">This is my little camera</p>
      <div class="qr" aria-label={`QR code LC:${code}`}>{raw(svg)}</div>
      <p class="stmt">Take a picture of this and press send</p>
      <p class="code">LC:{code}</p>
      <p class="small">
        <a href={apexUrl}>Little camera</a> · {email} ·{' '}
        <form method="post" action={u('/logout')} style="display:inline">
          <button type="submit" style="text-decoration:underline">sign out</button>
        </form>
      </p>
    </div>
    <script>{raw(waitForCameraScript(blogUrl))}</script>
  </Page>
);

/** Flips the page to the blog URL the moment the camera photographs the code, so nobody has to reload. */
function waitForCameraScript(blogUrl: string): string {
  return `(function(){
  var statusUrl=${JSON.stringify(u('/me/status'))}, meUrl=${JSON.stringify(u('/me'))};
  var timer=setInterval(function(){
    fetch(statusUrl,{headers:{accept:'application/json'}}).then(function(r){return r.json()}).then(function(j){
      if(!j.bound)return;
      clearInterval(timer);
      document.getElementById('verify').innerHTML=
        '<p class="stmt">Linked</p><p class="url"><a href="'+j.url+'">'+j.url+'</a></p>'+
        '<p class="small"><a href="'+meUrl+'">Your page</a></p>';
    }).catch(function(){});
  },3000);
})();`;
}

const avatarLine = (p: { avatar_requested_at: number | null; avatar_photo_id: number | null }): string => {
  if (p.avatar_requested_at) return 'The next picture you take becomes your profile picture.';
  return p.avatar_photo_id ? 'You have a profile picture.' : 'No profile picture yet.';
};

export const MyBlogPage: FC<{
  blog: { name: string; avatar_requested_at: number | null; avatar_photo_id: number | null };
  blogUrl: string;
  shortCode: string;
  subscribers: SubscriberRow[];
}> = ({ blog, blogUrl, shortCode, subscribers }) => (
  <Simple title="Your little camera" heading="Your little camera">
    <p>Your blog: <a href={blogUrl}>{blogUrl}</a></p>
    <p>Camera <code>{shortCode}</code> is linked. {avatarLine(blog)}</p>
    <form method="post" action={u('/me/request-avatar')}>
      <button class="btn" type="submit">{blog.avatar_photo_id ? 'Replace profile picture' : 'Request profile picture'}</button>
    </form>

    <h1 style="margin-top:28px">Subscribers</h1>
    {subscribers.length === 0 ? <p>Nobody yet.</p> : null}
    <ul class="list">
      {subscribers.map((s) => (
        <li>
          <span>
            {s.name ? `${s.name} · ` : ''}{s.email}
            <span class="tag">{s.status}</span>
          </span>
          <span>
            {s.status !== 'approved' ? (
              <form method="post" action={u(`/me/subscribers/${s.id}/approve`)}>
                <button class="btn" type="submit">Approve</button>
              </form>
            ) : null}{' '}
            {s.status !== 'blocked' ? (
              <form method="post" action={u(`/me/subscribers/${s.id}/block`)}>
                <button class="btn" type="submit">Block</button>
              </form>
            ) : null}
          </span>
        </li>
      ))}
    </ul>
    <form method="post" action={u('/me/subscribers')} class="row">
      <label class="field">
        <span>Email</span>
        <input name="email" type="email" required />
      </label>
      <label class="field">
        <span>Name (optional)</span>
        <input name="name" maxlength={60} />
      </label>
      <button class="btn" type="submit">Add</button>
    </form>
    <form method="post" action={u('/logout')} style="margin-top:28px">
      <button class="btn" type="submit">Sign out</button>
    </form>
  </Simple>
);

export const Mailbox: FC<{ rows: { id: number; subject: string; to_email: string; status: string; when: string }[] }> = ({ rows }) => (
  <Simple title="Dev mailbox" heading="Dev mailbox">
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
