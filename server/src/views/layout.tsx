// The one stylesheet and page shell for every HTML page the server sends.
// The CSS is the blog mockup's, nearly verbatim; the handful of extra
// classes at the end (.page, .field, .verify, .notice, .teaser) reuse the
// same tokens so the registration and /me pages look like the blog.
// Black and white only. The only "grey" is the 2 px checker (.dither50).

import type { FC, Child } from 'hono/jsx';
import { raw } from 'hono/html';

export const CSS = `
  :root{--ink:#000;--paper:#fff;--rule:1px solid #000;--pad:14px}
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
  html,body{margin:0;padding:0;background:var(--paper);color:var(--ink);
       -webkit-text-size-adjust:100%;text-size-adjust:100%}
  body{font-family:"Helvetica Neue",Helvetica,Inter,system-ui,-apple-system,Arial,sans-serif;
       font-size:14px;line-height:1.4;-webkit-font-smoothing:antialiased}
  /* the only "grey" on the page is a 2px checker */
  .dither50{background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='2' height='2'><rect width='1' height='1' x='0' y='0' fill='black'/><rect width='1' height='1' x='1' y='1' fill='black'/></svg>");background-size:2px 2px}

  .wrap{max-width:440px;margin:0 auto;min-height:100vh;border-left:var(--rule);border-right:var(--rule)}
  @media (max-width:460px){.wrap{border-left:0;border-right:0}}

  header{position:sticky;top:0;z-index:10;background:var(--paper);border-bottom:var(--rule);
         display:flex;align-items:center;gap:10px;padding:10px var(--pad)}
  header img{width:44px;height:44px;display:block;border:var(--rule);image-rendering:pixelated}
  .title{flex:1;min-width:0}
  .title b{display:block;font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase}
  .title a{color:inherit;text-decoration:none}
  .batt{display:flex;align-items:center;gap:6px;font-size:10px;letter-spacing:.09em;text-transform:uppercase}
  .cell{width:22px;height:9px;border:var(--rule);padding:1.5px;position:relative;flex:none}
  .cell::after{content:"";position:absolute;right:-4px;top:2px;width:2px;height:5px;background:#000}
  .cell i{display:block;height:100%}

  button{font:inherit;touch-action:manipulation;color:inherit;background:none;border:0;padding:0;cursor:pointer}
  /* Links inherit ink everywhere: the browser's default blue is the one colour that sneaks onto a page uninvited. */
  a{color:inherit}
  .btn{border:var(--rule);background:var(--ink);color:var(--paper);font-size:10px;font-weight:700;
       letter-spacing:.1em;text-transform:uppercase;padding:7px 10px;white-space:nowrap;text-decoration:none;display:inline-block}
  .btn:active,.btn[aria-pressed="true"]{background:var(--paper);color:var(--ink)}

  .subscribe-row{display:none;border-bottom:var(--rule);padding:10px var(--pad)}
  .subscribe-row.open,.subscribe-row:target{display:flex;gap:8px}
  .subscribe-row input{flex:1;min-width:0;font-family:inherit;font-size:16px;border:0;border-bottom:var(--rule);
       border-radius:0;background:none;padding:4px 0}

  article{border-bottom:var(--rule)}
  .meta{padding:9px var(--pad) 8px;font-size:10px;letter-spacing:.11em;text-transform:uppercase;
        display:flex;gap:0 7px}
  .shot{display:block;width:100%;height:auto;image-rendering:pixelated;
        border-top:var(--rule);border-bottom:var(--rule)}

  .comments{padding:10px var(--pad) 10px;font-size:13.5px}
  .comments ul{margin:0;padding:0}
  .comments li{list-style:none;margin:0 0 3px;display:flex;gap:6px}
  .comments b{font-weight:700;flex:none}
  .cmtform{display:flex;gap:8px;padding:0 var(--pad) 11px;flex-wrap:wrap}
  .cmtform input{flex:1;min-width:0;font-family:inherit;font-size:16px;border:0;border-bottom:var(--rule);
        border-radius:0;background:none;padding:3px 0}
  .cmtform input[name=name]{flex:0 1 38%}
  .cmtform button{font-size:11px;font-weight:700;letter-spacing:.08em;text-transform:uppercase}

  /* iOS Safari zooms the viewport on focus if an input is under 16px.
     Keep every field at exactly 16px rather than locking pinch-zoom. */
  input{font-size:16px;line-height:1.3}
  input::placeholder{opacity:1;font-size:16px}

  .more{display:block;width:100%;padding:15px;font-size:10px;font-weight:700;text-align:center;color:inherit;
        letter-spacing:.12em;text-transform:uppercase;margin-bottom:24px;text-decoration:none}
  .more:disabled,.more.end{cursor:default}
  :focus-visible{outline:2px solid #000;outline-offset:2px}

  /* additions for the non-blog pages, same tokens */
  .page{padding:18px var(--pad) 40px}
  .page h1{font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase;margin:0 0 18px}
  .page p{margin:0 0 12px;font-size:15px}
  .page a{color:inherit}
  .page ul{margin:0 0 12px;padding:0 0 0 18px}
  .field{display:block;margin:0 0 16px}
  .field span{display:block;font-size:10px;letter-spacing:.11em;text-transform:uppercase;margin-bottom:2px}
  .field input{width:100%;font-family:inherit;font-size:16px;border:0;border-bottom:var(--rule);border-radius:0;background:none;padding:4px 0}
  .row{display:flex;gap:8px;align-items:flex-end}
  .row .field{flex:1;margin:0}
  .err{font-size:12px;letter-spacing:.06em;text-transform:uppercase;margin:0 0 14px;padding:8px 10px;border:var(--rule)}
  .notice{border-bottom:var(--rule);padding:10px var(--pad);font-size:10px;letter-spacing:.11em;text-transform:uppercase}
  .teaser{padding:24px var(--pad) 40px;font-size:10px;font-weight:700;letter-spacing:.12em;text-transform:uppercase;text-align:center}
  .list{margin:0 0 18px;padding:0;list-style:none;border-top:var(--rule)}
  .list li{display:flex;gap:8px;align-items:center;justify-content:space-between;padding:8px 0;border-bottom:var(--rule);font-size:13.5px}
  .list li form{display:inline}
  .tag{font-size:10px;letter-spacing:.09em;text-transform:uppercase;margin-left:6px}
  .stmt{font-size:13px;font-weight:700;letter-spacing:.14em;text-transform:uppercase;margin:0;text-align:center}
  .verify{min-height:100vh;min-height:100dvh;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:16px;padding:14px;text-align:center}
  .verify .qr{width:min(92vw,calc(100dvh - 210px));max-width:640px}
  .verify .qr svg{display:block;width:100%;height:auto;shape-rendering:crispEdges}
  .verify .code{font-size:11px;letter-spacing:.3em;margin:0}
  .verify .small{font-size:12px;margin:8px 0 0}
  .verify .small form{display:flex;gap:8px;justify-content:center;align-items:flex-end;margin-top:6px}
  .verify .small input{width:9em;font-family:inherit;font-size:16px;letter-spacing:.2em;text-transform:uppercase;text-align:center;border:0;border-bottom:var(--rule);border-radius:0;background:none;padding:2px 0}
  .verify .url{font-size:15px;word-break:break-all}
  .mail{font-size:13px}
  .mail pre{white-space:pre-wrap;border:var(--rule);padding:10px;font:12px/1.4 ui-monospace,Menlo,monospace}
  .mail iframe{width:100%;height:520px;border:var(--rule);background:#fff}
`;

export const Page: FC<{
  title: string;
  description?: string;
  /** Wrap in the 440 px column (default). The QR page wants the whole viewport instead. */
  wrap?: boolean;
  head?: Child;
  children?: Child;
}> = ({ title, description, wrap = true, head, children }) => (
  <html lang="en">
    <head>
      <meta charset="utf-8" />
      <meta name="viewport" content="width=device-width, initial-scale=1" />
      <title>{title}</title>
      {description ? <meta name="description" content={description} /> : null}
      <meta name="color-scheme" content="light" />
      <meta name="theme-color" content="#ffffff" />
      {head}
      <style>{raw(CSS)}</style>
    </head>
    <body>{wrap ? <div class="wrap">{children}</div> : children}</body>
  </html>
);

/** A bare page with an uppercase heading and some paragraphs. */
export const Simple: FC<{ title: string; heading?: string; children?: Child }> = ({ title, heading, children }) => (
  <Page title={title}>
    <div class="page">
      <h1>{heading ?? title}</h1>
      {children}
    </div>
  </Page>
);
