// `node tools/check-page.mjs [url]` — does the landing page boot?
//
// Two classic scripts share one lexical scope, so a `const` the element and the
// page both declare is a SyntaxError that kills whichever parses second. The
// page still paints, because the layout is CSS, and the only symptom is the
// device quietly falling back: no dither, no turn, no screens. That is what
// this catches. Needs Chrome; nothing is installed.

import { spawn } from 'node:child_process';
import { rmSync } from 'node:fs';
import { setTimeout as sleep } from 'node:timers/promises';

const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const PORT = 9470;
const url = process.argv[2] ?? 'http://127.0.0.1:8077/';
const profile = `${process.env.TMPDIR ?? '/tmp'}/lc-check-${process.pid}`;

const chrome = spawn(CHROME, [
  '--headless=new', `--remote-debugging-port=${PORT}`, `--user-data-dir=${profile}`,
  '--no-first-run', '--hide-scrollbars', '--enable-unsafe-swiftshader', '--use-angle=swiftshader',
], { stdio: 'ignore' });

let id = 0;
const pending = new Map();
const thrown = [];

const endpoint = await (async () => {
  for (let i = 0; i < 60; i++) {
    try {
      return (await (await fetch(`http://127.0.0.1:${PORT}/json/version`)).json()).webSocketDebuggerUrl;
    } catch { await sleep(250); }
  }
  throw new Error('chrome never came up');
})();

const ws = new WebSocket(endpoint);
await new Promise((r) => ws.addEventListener('open', r));
ws.addEventListener('message', (e) => {
  const m = JSON.parse(e.data);
  if (m.method === 'Runtime.exceptionThrown') {
    const d = m.params.exceptionDetails;
    thrown.push(d.exception?.description ?? d.text);
  }
  if (m.id && pending.has(m.id)) { pending.get(m.id)(m.result); pending.delete(m.id); }
});
const send = (method, params = {}, sessionId) => {
  const i = ++id;
  ws.send(JSON.stringify({ id: i, method, params, ...(sessionId ? { sessionId } : {}) }));
  return new Promise((r) => pending.set(i, r));
};

const { targetId } = await send('Target.createTarget', { url: 'about:blank' });
const { sessionId } = await send('Target.attachToTarget', { targetId, flatten: true });
await send('Page.enable', {}, sessionId);
await send('Runtime.enable', {}, sessionId);
await send('Emulation.setDeviceMetricsOverride', { width: 1200, height: 860, deviceScaleFactor: 1, mobile: false }, sessionId);
await send('Page.navigate', { url }, sessionId);
await sleep(6000);

const probe = await send('Runtime.evaluate', {
  returnByValue: true,
  expression: `(() => {
    const cam = document.querySelector('little-camera');
    return {
      element: !!customElements.get('little-camera'),
      ready: !!(cam && cam.ready),
      // Only the page's own script adds this, so it proves the inline script ran.
      lit: document.querySelector('.stage').classList.contains('gl-on'),
      morphs: typeof (cam && cam.morph),
    };
  })()`,
}, sessionId);

ws.close();
chrome.kill();
rmSync(profile, { recursive: true, force: true, maxRetries: 5 });

const got = probe.result?.value ?? {};
const fail = [];
for (const err of thrown) fail.push(`threw: ${err.split('\n')[0]}`);
if (!got.element) fail.push('<little-camera> was never defined');
if (!got.ready) fail.push('the element never got a WebGL context');
if (!got.lit) fail.push("the page's own script did not run: the device is on its CSS fallback");
if (got.morphs !== 'function') fail.push('morph() is missing');

console.log(`${url} -> ${JSON.stringify(got)}`);
if (fail.length) {
  for (const f of fail) console.error(`FAIL ${f}`);
  process.exit(1);
}
console.log('ok');
