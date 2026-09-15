// Blog markup, matching the mockup: sticky header, subscribe row,
// articles (meta, shot, comments, comment form), load more.

import type { FC } from 'hono/jsx';
import { raw } from 'hono/html';
import type { PhotoRow } from '../repo/photos.js';
import type { CommentView } from '../repo/comments.js';

/** "Mees" → "Mees’s little camera" (CSS uppercases it to MEES’S LITTLE CAMERA). A real
 * apostrophe rather than ' because JSX would escape the ASCII one to &#39;. */
export function blogTitle(name: string): string {
  return `${name}’s little camera`;
}

export type Viewer =
  | { kind: 'anonymous' }
  | { kind: 'owner' }
  | { kind: 'subscriber'; id: number; name: string | null };

export const BlogHeader: FC<{ name: string; battery: number; viewer: Viewer; meUrl: string }> = ({ name, battery, viewer, meUrl }) => (
  <header>
    <a href="/">
      <img src="/avatar.png" width="44" height="44" alt="Profile picture" />
    </a>
    <div class="title">
      <b>
        <a href="/">{blogTitle(name)}</a>
      </b>
      <span class="batt">
        <span class="cell">
          <i class="dither50" style={`width:${battery}%`}></i>
        </span>
        {battery}%
      </span>
    </div>
    {viewer.kind === 'anonymous' ? (
      <a class="btn" id="subbtn" href="#join" aria-expanded="false">
        Subscribe
      </a>
    ) : viewer.kind === 'owner' ? (
      <a class="btn" href={meUrl}>
        Me
      </a>
    ) : (
      <span class="btn" aria-pressed="true">
        Subscribed
      </span>
    )}
  </header>
);

export const SubscribeRow: FC<{ open: boolean }> = ({ open }) => (
  <form class={open ? 'subscribe-row open' : 'subscribe-row'} id="join" method="post" action="/subscribe">
    <input id="email" name="email" type="email" placeholder="you@email.com" aria-label="Email address" required autocomplete="email" />
    <button class="btn" id="subgo" type="submit">
      Join
    </button>
  </form>
);

export const Notice: FC<{ text: string }> = ({ text }) => <div class="notice">{text}</div>;

export interface ArticleData {
  photo: PhotoRow;
  date: string;
  time: string;
  comments: CommentView[];
}

export const Article: FC<{ a: ArticleData; viewer: Viewer }> = ({ a, viewer }) => {
  const canComment = viewer.kind !== 'anonymous';
  const needsName = viewer.kind === 'subscriber' && !viewer.name;
  return (
    <article id={`p-${a.photo.public_id}`}>
      <div class="meta">
        <span>{a.date}</span>
        <span>·</span>
        <span>{a.time}</span>
      </div>
      <img class="shot" width="320" height="240" alt="Photo" src={`/photos/${a.photo.public_id}.png`} />
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

export const LoadMore: FC<{ href: string | null }> = ({ href }) =>
  href ? (
    <a class="more" href={href}>
      Load more
    </a>
  ) : (
    <div class="more end">No more photos</div>
  );

/**
 * The only script on the blog. Everything works without it; with it the
 * subscribe button slides the email row open, joining does not leave the
 * page, and a posted comment appears immediately.
 */
export const BLOG_SCRIPT = `
(function(){
  var btn=document.getElementById('subbtn'),row=document.getElementById('join');
  if(btn&&row){
    btn.addEventListener('click',function(e){
      e.preventDefault();
      var open=row.classList.toggle('open');
      btn.setAttribute('aria-expanded',String(open));
      if(open)document.getElementById('email').focus();
    });
    row.addEventListener('submit',function(e){
      e.preventDefault();
      var email=document.getElementById('email');
      if(!email.value.includes('@')){email.focus();return;}
      fetch('/subscribe',{method:'POST',headers:{'content-type':'application/json',accept:'application/json'},
        body:JSON.stringify({email:email.value})})
        .then(function(r){return r.json()})
        .then(function(j){
          row.classList.remove('open');
          btn.textContent=j.state==='already'?'Subscribed':'Requested';
          btn.setAttribute('aria-pressed','true');
        })
        .catch(function(){row.submit();});
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

export const BlogScript: FC = () => <script>{raw(BLOG_SCRIPT)}</script>;
