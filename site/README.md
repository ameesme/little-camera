# lttl.cam

The teaser page. One file, no build, no dependencies: `index.html` is
self-contained (the firmware screens are inlined as base64 PNGs), so it can be
dropped on any static host or served by Caddy with a two-line site block.

The device is a placeholder: a rounded grey slab at roughly the real
dimensions. The 2.7" 400x240 Sharp panel is 57.6 x 34.56 mm of glass, and 3 mm
of bezel on every side makes the body 63.6 x 40.6 x 7 mm. There is no chin: the
border is the same width all round, and the body is only as tall as that makes
it. The shell's corner radius is 3.5 mm and the panel has 1.75 mm of its own.
Replace it when the industrial design lands; every proportion is derived from
that rectangle, so changing `--w`, `--h`, `--d` and `--r` at the top of the
stylesheet is enough.

## The back

One thing lives on the back: the lens, up in the top left seen from behind. It
sits about 8 mm in from each of the two edges it is near, which is what keeps
the whole barrel on the flat of the back rather than climbing the shell's
rounded edge.

The barrel is raised 1.3 mm and blended into the back with a smooth union, so
it grows out of the shell rather than standing on it, and the glass is sunk
0.8 mm into its face. Only the glass is dark; the barrel is the shell's own
colour, carried by that blend and by the contact shading at its foot — the line
a raised part lays down on what it stands on, which is what tells the eye it is
raised rather than drawn. A hard step would not survive: an extrusion shares
its surface normal with the panel it stands on, so the two shade identically
and the shape vanishes.

In the shader, seen from behind, `+x` runs to the viewer's left; `P_LENS` and
the `R_*` and `LENS_*` constants next to it are the whole layout, in units
where the device is one wide.

## The screens

The four panels on the device are real frames, rendered by the firmware's own
preview tool rather than mocked up:

| Screen | Scene | Source photo |
|---|---|---|
| asleep (two breath frames) | `sleep`, `sleep --breath` | drawn by the firmware |
| viewfinder | `viewfinder --src` | `packages/pbm/fixtures/photographer.pbm` |
| posted | `gallery --pbm` | `packages/pbm/fixtures/cat.pbm` |

To swap in new pictures, render them and replace the matching base64 string in
the `SCREENS` object at the top of the page's script:

```
cd firmware/tools/preview && make
./preview gallery --pbm <photo>.pbm --index 2 --total 12 --scale 1 --out out/gallery.bmp
```

The panel is 400x240 and 1-bit, so keep the replacements at that size and
`image-rendering: pixelated` does the rest.

## The film

A recording of this page, zoomed until the dither came apart, running at full
strength behind the headline with the words inverted on top of it — and masked
down to the lines themselves, so it reads as a selected surface rather than a
panel.

The inversion is the blend. White letters in `mix-blend-mode: difference` come
out as 255 minus whatever the film is doing behind each one, so they are always
the exact opposite of their own backdrop and can never sink into it, however
the film moves. `isolation: isolate` keeps that reckoning inside the title,
against the film, rather than against the ground the title sits on. It is the
page's own rule — if a design needs emphasis, invert — doing the work.

The mask is three solid gradient layers, one per line, placed and sized from
the lines' own boxes once the headline has been fitted. A `clip-path` cannot
describe three disjoint rectangles, and three `<video>` elements would mean
three decoders. The lines are `width: fit-content` so their boxes hug the
words, and the bands are bled a little past them, because the leading is 0.72
and the letters overflow their line boxes. The heading is padded for the same
reason.

The film's width and height are set rather than left to the insets — a video is
a replaced element, so `width: auto` takes its own 540x926 instead of
stretching.

Both layers are thresholded back to two values with
`filter: grayscale(1) contrast(2000%)`. The clip is 1-bit to begin with, but
the encode, the chroma subsampling and the scale on the way to the screen turn
those two levels into a spread — and a spread laid over a dithered ground gives
muddy half-tones where there should be dots. Enormous contrast is a threshold:
everything either side of mid-grey clamps, so what comes out is black or white
and nothing between. Grayscale first, because a subsampled encode of grey is
not exactly grey.

`media/ground.mp4` is H.264 at 540x926, 24fps, ~790 KB, and comes first because
it is what every iOS Safari decodes; `media/ground.webm` is VP9 for builds
without the proprietary codecs. The source was HEVC in a QuickTime container,
which only Safari plays. To replace it:

```
ffmpeg -i <clip>.mov -an -vf "fps=24,scale=540:-2:flags=area" \
  -c:v libx264 -profile:v main -pix_fmt yuv420p -crf 30 -preset slow \
  -movflags +faststart site/media/ground.mp4
```

The block itself is multiplied onto the page, so the white of the words becomes
whatever the ground is under them — its grey, its dither, its gradient — while
the block's black stays black. `isolation: isolate` on the same element keeps
the inversion inside from taking part: the difference blend resolves first, at
full strength, and only the finished block is multiplied onto the ground.

For that to reach the ground at all, nothing between the two may be a stacking
context, which is why `.sheet` carries no `z-index`. It only needs to paint
after the ground, and document order already does that; a `z-index` there would
trap the blend inside the sheet, against nothing.

The same film runs a second time over the whole page at 14%, enough that the
paper reads as a shade rather than a flat fill. That is a second element and so
a second decoder, which is the price of having the film at two strengths at
once: one element cannot be in two places.

A radial mask keeps that wash off the middle entirely. Nothing at all out to
60%, where the camera and the words live, and from there it climbs to full at
the corners — the grain is something the page gathers as it runs out of the
frame.

It sits under the sheet, which means the device's canvas has to let it through
or the canvas would be a clean rectangle in the wash, a pale frame around the
camera. So the canvas is opaque where the ray hit the device and transparent
where it missed. The void's ink dots are still drawn, so the shadow and the
vignette's grain survive, and since they come from the same page-aligned field
as the ground underneath they land on exactly the dots already there.

Autoplay is refused in low power mode even when muted and inline, so the first
touch starts both instead, and nothing depends on them playing.

## The card

`media/og.png` is a real frame of this page rather than a mock-up: the same
ground, the same dither, the same glitched headline, the device held face on
showing a photo. It is 1200x630 at one device pixel per CSS pixel, because the
dither is one dot per CSS pixel and scaling it would turn the dots to mush.

It is laid out landscape rather than in the page's own stack — device left,
words right — because a portrait composition in a 1200x630 frame leaves most of
it empty. To remake it, load the page at that size, turn the sheet into a row,
pin the device face on, swap the credit for the spec line, and shoot:

```js
document.documentElement.style.setProperty('--sheet', '470px');
const sheet = document.querySelector('.sheet');
sheet.style.width = '1080px';
sheet.style.flexDirection = 'row-reverse';   // the stage is second in the DOM
sheet.style.alignItems = 'center';
sheet.style.gap = '64px';
document.querySelector('.stage').style.flex = '0 0 auto';
document.querySelector('.say').style.flex = '1 1 auto';
document.querySelector('.by').textContent =
  '0.07 megapixel \u2022 1-bit monochrome \u2022 uploads to a private microblog';
spinFrom = 0; spinTo = 0; spinAt = -1e9;
setScreen('gallery');
fitAndMask(); sizeGL();
```

Everything in the head is lower case, the way the page is set. The favicon is
an inline SVG of the device in ink on the page's paper, so there is no second
request and no 404 for `/favicon.ico`.

## Type and colour

Both come from amaranthstudio.com, so the teaser sits in the same world:

| | |
|---|---|
| Ground | `#ddd` |
| Ink | `#000` |
| Body | PP Neue Montreal Book, 400 |
| Headline | PP Neue Montreal Bold, 700, lower case |

Lower case because every heading on amaranthstudio.com is. The micro-blog's
tracked capitals are right at 10px and wrong at 90. Leading is 0.72, tight
enough that the ascenders of one line cross the baseline of the one above:
the dot of "is" falls into the "a" of "camera".

The font files are licensed; they live in `fonts/` and are covered by Amaranth
Studio's webfont licence. See `fonts/README.md`.
The studio's accent (`#f2385a`) is deliberately unused: this project is black
and white. Its dark ground (`#111`) is unused too, the page being light only.

## Design

Tokens and hairlines are the micro-blog's (`server/src/views/layout.tsx`): ink
on paper, Helvetica, 1px rules. The headline is the one deliberate departure
from the blog's tiny tracked caps, because at display size Helvetica Bold wants
tight tracking rather than wide. The page is black and white; the only greys
belong to the device, which has to be a solid object in order to float.

The device is raymarched as a signed distance field in WebGL and dithered, so
the page renders it the same way the camera renders the world. The dither's
two levels are the page's own ink and ground, so the canvas has no edge. The
matrix is the firmware's own 4x4 Bayer (`BAYER4` in `display.cpp`) recursed
once to 8x8, the way Bayer matrices are built: sixteen levels are right for a
400x240 panel and wrong for a page, where a gradient across a whole window
quantises into visible rings. It is applied in screen space at one dot per CSS
pixel, which on a phone lands close to the physical pixel pitch of the real
panel. Output is strictly black and white: no intermediate values reach the
canvas.

The whole page is that dither, not just the device. A second canvas, fixed
behind the content, carries a radial gradient from the middle of the viewport
out, screened with the same matrix on the same page-aligned grid. The device's
shader indexes both the gradient and the grid in page coordinates, which is
what `uPage` is for, so there is no seam where its canvas starts. `uPage` is
re-read every frame, because the sheet moves under the canvas whenever the
headline is refitted.

The gradient is paper for the middle 70 per cent and then a straight ramp to
0.30 darker at the corners. An ordered dither lays each new dot exactly on the
lattice, so a linear ramp bands into evenly spaced rings — which is the point:
the rings are the gradient, the same way the camera's own pictures are made of
them.

Screen space is the whole point. A CSS pattern would rotate and foreshorten
with the object and read as texture printed on it, so the object has to be
rendered rather than assembled from CSS faces. The panel is not shaded, because
it is 1-bit in real life, and its texture is sampled smoothly and then dithered
with everything else, which re-screens the photograph at render resolution
rather than moireing the dither it already carries.

The shadow is a soft ellipse in screen space, not a shadow cast on a floor: the
void has no floor, and a ground plane at this grazing an angle smears into a
streak running to the horizon.

The CSS device it replaced is still in the markup as a fallback. If WebGL fails
to start, the canvas stays hidden and the CSS version runs instead.

One measure drives the whole composition. `--sheet` is the block's width: as
wide as the screen allows, capped at 480px and at 48svh so the pair stays whole
on one screen. The stack is about 1.6 measures tall, so that height cap is
what keeps the page free of scrollbars; it is the *small* viewport height,
because `dvh` would resize the composition every time a mobile URL bar slid
away and every resize re-fits the headline. Everything else is a fraction of
the measure, so the composition scales as one object and can never be wider
than the screen:

| Part | Size |
|---|---|
| headline | `--sheet` x 0.168 (a floor; the fitter takes over) |
| credit line | `--sheet` x 0.048 |
| camera | `--sheet` x 0.76 |
| gap between them | `--sheet` x 0.12 |

The credit — "a project by amaranth studio", linking to amaranthstudio.com —
sits under the headline, in the flow, aligned to the same left edge. It is set
in Book rather than Bold, at a little over a quarter of the headline's size,
with no underline — and it is inverted the same way the headline is. White
letters in `mix-blend-mode: difference` come out as the exact opposite of the
ground under them, so the line carries the page's own dots reversed rather than
a colour of its own. Nothing between it and the ground may be a stacking
context, which is the other reason `.sheet` has no `z-index`. The page does not scroll: at every
width the document is exactly the viewport.

The headline is fitted at runtime rather than calculated. Predicting the line
width from font metrics was wrong on real devices: iOS renders Helvetica Neue,
whose Bold is fractionally wider than Helvetica or Arial, so the longest line
wrapped there and the three lines became four. The script now measures the
lines in whatever font actually resolved and sets the size so the widest one
fills the measure exactly. The 0.168 factor in the stylesheet is only the
pre-script fallback, deliberately short of the measure.

Each line is its own block with `white-space: nowrap`, so three lines stay
three lines whatever the font does. The camera is on top and the headline
beneath it, centred in the viewport: the object is what the page is about, and
the words caption it. The block is centred horizontally with the text left
aligned inside it and the camera centred above.

The markup keeps the words first and flips the pair with `column-reverse`, so
the document still opens with its own title and a screen reader meets the words
before the ornament. The sheet is centred with `margin:auto` rather than by
centring on the body, because a flex container centred with `align-items` clips
the top of anything taller than it: on a window too short for the composition
it should scroll, not lose its head.
