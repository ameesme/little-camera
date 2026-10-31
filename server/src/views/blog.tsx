import type { FC } from 'hono/jsx';
import { raw } from 'hono/html';
import type { Viewer } from '../core/blogs.js';
import type { CommentView } from '../repo/comments.js';
import type { PhotoRow } from '../repo/photos.js';
import type { BlogRow } from '../repo/blogs.js';
import { Page, Stage } from './layout.js';

// A real apostrophe, because JSX escapes the ASCII one to &#39;. CSS does the uppercasing.
export function blogTitle(name: string): string {
  return `${name}’s little camera`;
}

const BlogHeader: FC<{ name: string; battery: number | null; viewer: Viewer }> = ({ name, battery, viewer }) => (
  <header>
    <a href="/">
      <img src="/avatar.png" width="44" height="44" alt="Profile picture" />
    </a>
    <div class="title">
      <b>
        <a href="/">{blogTitle(name)}</a>
      </b>
      {battery === null ? null : (
        <span class="batt">
          <span class="cell">
            <i class="dither50" style={`width:${battery}%`}></i>
          </span>
          {battery}%
        </span>
      )}
    </div>
    {viewer.kind === 'anonymous' ? (
      <a class="btn" href="#join">
        Log in
      </a>
    ) : (
      <span class="btn" aria-pressed="true">
        Subscribed
      </span>
    )}
  </header>
);

/** The ask, under the one picture a visitor gets: the same form does joining and asking for a fresh link. */
const JoinBox: FC<{ name: string }> = ({ name }) => (
  <section class="join" id="join">
    <h2>View {name}&#8217;s little camera</h2>
    <p>If {name} lets you in, you will get an email.</p>
    <form method="post" action="/subscribe">
      <input id="email" name="email" type="email" placeholder="you@email.com" aria-label="Email address" required autocomplete="email" />
      <button class="btn" id="subgo" type="submit">
        Join
      </button>
    </form>
  </section>
);

const Notice: FC<{ text: string }> = ({ text }) => <div class="notice">{text}</div>;

export interface ArticleData {
  photo: PhotoRow;
  date: string;
  time: string;
  comments: CommentView[];
}

const Article: FC<{ a: ArticleData; viewer: Viewer }> = ({ a, viewer }) => {
  const canComment = viewer.kind === 'subscriber';
  const needsName = canComment && !viewer.name;
  return (
    <article id={`p-${a.photo.public_id}`}>
      <div class="meta">
        <span>{a.date}</span>
        <span>·</span>
        <span>{a.time}</span>
      </div>
      <img class="shot" width="320" height="240" alt="Photo" src={`/photos/${a.photo.public_id}.png`} />
      {viewer.kind !== 'anonymous' ? (
        <div class="comments">
          <ul>
            {a.comments.map((c) => (
              <li>
                <b>{c.name}</b>
                <span>{c.body}</span>
              </li>
            ))}
          </ul>
        </div>
      ) : null}
      {canComment ? (
        <form class="cmtform" method="post" action={`/p/${a.photo.public_id}/comments`}>
          {needsName ? <input name="name" placeholder="Your name" aria-label="Your name" maxlength={60} required /> : null}
          <input name="body" placeholder="Say something" aria-label="Write a comment" maxlength={1000} required />
          <button type="submit">Post</button>
        </form>
      ) : null}
    </article>
  );
};

const LoadMore: FC<{ href: string | null }> = ({ href }) =>
  href ? (
    <a class="more" href={href}>
      Load more
    </a>
  ) : (
    <div class="more end">No more photos</div>
  );

// The only script on the blog, and everything works without it.
const BLOG_SCRIPT = `
(function(){
  var row=document.getElementById('join');
  if(row){
    row.querySelector('form').addEventListener('submit',function(e){
      e.preventDefault();
      var email=document.getElementById('email');
      if(!email.value.includes('@')){email.focus();return;}
      fetch('/subscribe',{method:'POST',headers:{'content-type':'application/json',accept:'application/json'},
        body:JSON.stringify({email:email.value})})
        .then(function(r){return r.json()})
        .then(function(j){
          var name=row.querySelector('h2').textContent.replace('View ','').split('’')[0];
          row.querySelector('h2').textContent=j.state==='already'?'You got mail':'Asked '+name;
          row.querySelector('p').textContent=j.state==='already'
            ?'Use the link from your email to log in.'
            :'If they let you in, you will get an email.';
          row.querySelector('form').remove();
        })
        .catch(function(){row.querySelector('form').submit();});
    });
  }
  document.querySelectorAll('form.cmtform').forEach(function(form){
    form.addEventListener('submit',function(e){
      e.preventDefault();
      var data=new FormData(form),body=(data.get('body')||'').toString().trim();
      if(!body)return;
      fetch(form.action,{method:'POST',headers:{accept:'application/json'},body:new URLSearchParams(data)})
        .then(function(r){if(!r.ok)throw 0;return r.json()})
        .then(function(j){
          var li=document.createElement('li');li.innerHTML='<b></b><span></span>';
          li.querySelector('b').textContent=j.name;li.querySelector('span').textContent=j.body;
          form.parentNode.querySelector('.comments ul').appendChild(li);
          form.querySelector('[name=body]').value='';
          var n=form.querySelector('[name=name]');if(n)n.remove();
        })
        .catch(function(){form.submit();});
    });
  });
})();
`;

const BlogScript: FC = () => <script>{raw(BLOG_SCRIPT)}</script>;

const DESCRIPTION = 'Photos from a little camera. New ones arrive on their own.';

export interface BlogPage {
  blog: BlogRow;
  battery: number | null;
  avatarUrl: string;
  blogRoot: string;
  articles: ArticleData[];
  notice?: string;
}

const Shell: FC<{ page: BlogPage; children?: unknown }> = ({ page, children }) => (
  <Page
    title={blogTitle(page.blog.name)}
    description={DESCRIPTION}
    head={
      <>
        <meta property="og:type" content="website" />
        <meta property="og:site_name" content={blogTitle(page.blog.name)} />
        <meta property="og:title" content={blogTitle(page.blog.name)} />
        <meta property="og:description" content={DESCRIPTION} />
        <meta property="og:url" content={page.blogRoot} />
        <meta property="og:image" content={page.avatarUrl} />
        <link rel="icon" href="/avatar.png" type="image/png" />
      </>
    }
  >
    {children as never}
  </Page>
);

/** What an approved subscriber sees: the whole feed, one page at a time. */
export const Feed: FC<BlogPage & { viewer: Viewer; moreHref: string | null }> = (p) => (
  <Shell page={p}>
    <BlogHeader name={p.blog.name} battery={p.battery} viewer={p.viewer} />
    {p.notice ? <Notice text={p.notice} /> : null}
    <main id="feed">
      {p.articles.length === 0 ? <div class="teaser">No pictures yet</div> : null}
      {p.articles.map((a) => (
        <Article a={a} viewer={p.viewer} />
      ))}
    </main>
    {p.articles.length > 0 ? <LoadMore href={p.moreHref} /> : null}
    <BlogScript />
  </Shell>
);

/** What a visitor without a link sees: the newest picture, a count of the rest, and a way in. */
export const Teaser: FC<BlogPage & { hiddenCount: number }> = (p) => {
  const anonymous: Viewer = { kind: 'anonymous' };
  return (
    <Shell page={p}>
      <BlogHeader name={p.blog.name} battery={p.battery} viewer={anonymous} />
      {p.notice ? <Notice text={p.notice} /> : null}
      <main id="feed">
        {p.articles.map((a) => (
          <Article a={a} viewer={anonymous} />
        ))}
      </main>
      <div class="teaser">{hiddenLine(p.articles.length > 0, p.hiddenCount)}</div>
      <JoinBox name={p.blog.name} />
      <BlogScript />
    </Shell>
  );
};

function hiddenLine(hasPictures: boolean, hidden: number): string {
  if (!hasPictures) return 'No pictures yet';
  if (hidden === 0) return 'More pictures for subscribers';
  return `${hidden} more ${hidden === 1 ? 'picture' : 'pictures'} for subscribers`;
}

export const ExpiredLink: FC<{ email: string }> = ({ email }) => (
  <Stage
    title="This link has expired"
    heading="This link has expired"
    actions={<button class="big" type="submit" form="fresh">Send me a fresh link</button>}
  >
    <p>Links work for 48 hours. Ask for a fresh one and it is in your inbox in a moment.</p>
    <form id="fresh" method="post" action="/request-link">
      <input class="ask" type="email" name="email" value={email} placeholder="you@email.com" aria-label="Email address" required autocomplete="email" />
    </form>
  </Stage>
);

/** Asked before anything happens: the link lands in a mail client that may have prefetched it. */
export const UnsubscribePrompt: FC<{ name: string; email: string; token: string }> = ({ name, email, token }) => (
  <Stage
    title="Stop getting these?"
    heading="Stop getting these?"
    actions={
      <form method="post" action="/unsubscribe">
        <input type="hidden" name="t" value={token} />
        <button class="big" type="submit">Take me off the list</button>
      </form>
    }
  >
    <p>
      <b>{email}</b> comes off {name}’s little camera. No more mail, and the link you have stops working.
    </p>
    <p>Changed your mind? Close this.</p>
  </Stage>
);

export const UnsubscribeDone: FC<{ name: string }> = ({ name }) => (
  <Stage title="Off the list" heading="Off the list">
    <p>You will hear nothing more from {name}’s little camera. Ask again from the blog whenever you like.</p>
  </Stage>
);

export const FreshLinkSent: FC = () => (
  <Stage title="Check your email" heading="Check your email">
    <p>If that address is on the list, a fresh link is on its way.</p>
  </Stage>
);
