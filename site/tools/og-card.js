// Paste into the console with the page open at 1200x630, then screenshot to site/media/og.png.

const WIDE = 1080, GAP = 64;
document.documentElement.style.setProperty('--sheet', '540px');   // sizes the device
// Full bleed: a share image is cropped and letterboxed by whoever shows it,
// so the page's gutter and rounded corners read as a mistake, not a frame.
document.documentElement.style.setProperty('--gutter', '0px');
document.documentElement.style.setProperty('--bento', '0px');

const sheet = document.querySelector('.sheet');
const stage = document.querySelector('.stage');
const say   = document.querySelector('.say');
sheet.style.width = WIDE + 'px';
sheet.style.margin = 'auto';                 // the page pins it up; the card centres it
sheet.style.flexDirection = 'row-reverse';   // the stage is second in the DOM
sheet.style.alignItems = 'center';
sheet.style.gap = GAP + 'px';
stage.style.flex = '0 0 auto';

const col = WIDE - Math.round(stage.getBoundingClientRect().width) - GAP;
say.style.flex = '0 0 auto';
say.style.width = col + 'px';

const by = document.querySelector('.by');
const bySpan = by.querySelector('span');
// Two lines, broken at a bullet rather than wherever the column runs out, and
// at a measured bullet rather than a fixed one.
const parts = bySpan.textContent.split(' • ');
const rule = document.createElement('span');
const cs2 = getComputedStyle(by);
rule.style.cssText = 'position:absolute;visibility:hidden;white-space:nowrap';
rule.style.fontFamily = cs2.fontFamily; rule.style.fontSize = cs2.fontSize;
rule.style.fontWeight = cs2.fontWeight; rule.style.letterSpacing = cs2.letterSpacing;
document.body.append(rule);
const wide = (t) => { rule.textContent = t; return rule.getBoundingClientRect().width; };
const pair = (i) => [wide(parts.slice(0, i).join(' • ') + ' •'),
                     wide(parts.slice(i).join(' • '))];

// First the smallest type that lets *some* split fit the column at all: five
// items do not fit at full size however they are cut.
let need = Infinity;
for (let i = 1; i < parts.length; i++) need = Math.min(need, Math.max(...pair(i)) / col);
if (need > 1){
  const px = Math.floor(parseFloat(cs2.fontSize) / need);
  by.style.fontSize = px + 'px'; rule.style.fontSize = px + 'px';
}
// Then the LAST split that still fits, so the first line is filled.
let best = 1;
for (let i = 1; i < parts.length; i++){
  const [w1, w2] = pair(i);
  if (w1 <= col && w2 <= col) best = i;
}
rule.remove();
bySpan.innerHTML = parts.slice(0, best).join(' • ') + ' •<br>' +
                   parts.slice(best).join(' • ');
bySpan.style.background = 'none';        // no band on the card
bySpan.style.color = 'var(--ink)';
bySpan.style.padding = '0';
document.querySelector('.pre').style.display = 'none';

// The badge is placed against the page's face-on device; here the device is
// turned and pitched, so its corner has moved. Nudge it onto the device.
const burst = document.querySelector('.burst');
const cs = getComputedStyle(burst);
burst.style.right = (parseFloat(cs.right) +  0) + 'px';
burst.style.top   = (parseFloat(cs.top)   + 36) + 'px';

setScreen('gallery');
window.setScreen = () => {};
window.draw = function(){
  gl.uniform1f(uni.uYaw,  -32 * Math.PI / 180);   // negative yaw: viewer to the right
  gl.uniform1f(uni.uPitch, 22 * Math.PI / 180);   // positive pitch: viewer above
  gl.uniform1f(uni.uBob, 0);
  placeGL(); gl.drawArrays(gl.TRIANGLES, 0, 3);
  requestAnimationFrame(window.draw);
};
// paintVignette and paintShade too, not just sizeGL: both canvases are sized
// and placed from the box and only repaint on a window resize, so after the
// --gutter override the ground keeps the smaller size and leaves bare paper
// down the right and the bottom, and the shadow stays where the old bottom
// edge was, baked across the card.
fitAndMask(); sizeGL(); paintVignette(); paintShade();

const now = parseFloat(getComputedStyle(by).fontSize);
if (by.scrollWidth > col) by.style.fontSize = Math.floor(now * col / by.scrollWidth) + 'px';
