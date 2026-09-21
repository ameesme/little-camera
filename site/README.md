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

A recording of this page, zoomed until the dither came apart, laid back over it
at `opacity: .12`. The two dot grids never line up, so they interfere, and that
moire is the whole effect.

It is the one place on the page where intermediate greys appear: the layer is
composited rather than screened, so the output there is no longer strictly two
levels. Everything else still is. `.film { opacity }` is the dial.

`media/ground.mp4` is H.264 at 540x926, 24fps, ~790 KB, and comes first because
it is what every iOS Safari decodes; `media/ground.webm` is VP9 for builds
without the proprietary codecs. The source was HEVC in a QuickTime container,
which only Safari plays. To replace it:

```
ffmpeg -i <clip>.mov -an -vf "fps=24,scale=540:-2:flags=area" \
  -c:v libx264 -profile:v main -pix_fmt yuv420p -crf 30 -preset slow \
  -movflags +faststart site/media/ground.mp4
```

It is absolute over the whole document rather than fixed to the viewport.
Ordinary page content flows behind mobile Safari's bars without trouble; it is
*fixed* layers that get clipped at the bar's edge, and a video full of dots is
exactly that kind of layer. Scrolling it with the page costs nothing here,
since there is only a screenful and a bit to scroll.

Its height comes from JS, measured off the credit: the credit is absolutely
positioned, so the document is taller than any box the film could inherit from,
and measuring `scrollHeight` instead would feed the film's own height back into
itself. The CSS value is only what holds until that runs. Like the 30px above
the top it is deliberately generous — every viewport unit comes up short
somewhere on iOS, and nobody notices a background sixty pixels too big.

The body is sized in `svh` rather than `dvh` for the film's sake. `dvh` tracks
the bars as they slide, so every scroll on a phone would change the body's
height, which moves the credit, which is what the film measures itself against
— the whole background would shift as you scrolled. `svh` is the viewport at
its smallest and never changes.

Autoplay is refused in low power mode even when muted and inline, so the first
touch starts it instead, and nothing depends on it playing.

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

The canvas is opaque where the device is and transparent where it is not. Both
halves dither to the same two levels, but where the ray hit something the paper
level is painted and the canvas is solid, so nothing behind the page shows
through the camera; where it missed, paper is left transparent and only the
shadow's ink dots are contributed. Filling the void as well made the render an
opaque rectangle sitting on top of the film — a white frame around the camera.
Leaving the device transparent too put the film straight through it.

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
is a full-width band at `top: 120%`, below the fold, centred on the page: down
there it is a footer, not a fourth line of the headline. It is set in Book
rather than Bold, at a little over a quarter of the headline's size, with no
underline and no colour of its own. Putting it down
there also gives the document about a hundred pixels to scroll, which is what
Safari wants before it will composite real pixels behind its own bars.

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
