# lttl.cam

The teaser page. One file, no build, no dependencies: `index.html` is
self-contained (the firmware screens are inlined as base64 PNGs), so it can be
dropped on any static host or served by Caddy with a two-line site block.

The device is a placeholder: a rounded grey slab at roughly the real
dimensions. The 2.7" 400x240 Sharp panel is 57.6 x 34.56 mm of glass and 1.75
mm of bezel at the sides makes the body 61.1 mm wide; its height is a
compromise between the L on the back, which wants a tall body, and the screen,
which wants a thin chin, and lands at 61.1 x 48 x 7 mm -- 6.7 mm above and
below the glass. The glass has a 1.75 mm corner radius of its own, the width of
its own bezel. The front is all screen, with no button on
it. The shell's corner radius is 3.5 mm and the glass radius is that less the
bezel, so the two curves stay concentric. Replace it when the industrial design
lands; every proportion is derived from that rectangle, so changing `--w`,
`--h`, `--d` and `--r` at the top of the stylesheet is enough.

## The back

Three things live on an L-shaped boss on the back, and the L is why the top and
bottom bezels are wider than the sides:

| Part | Where |
|---|---|
| lens | the geometric centre of the back |
| piezo hole | out to the left (seen from behind), on the same centre line |
| shutter | directly above the piezo, in the corner |

The L stands 1.8 mm proud of the back. Its foot runs from the lens out to the
piezo and its upright from there up to the shutter, both the same 11.2 mm
thickness. The foot is longer than the upright, so the L is wider than it is
tall, in the proportion of the body it sits on: each arm runs out to the same
small margin from its own edge, 3 mm at the side and 1.7 mm at the top, rather
than both stopping at the shorter of the two.

One thickness throughout means every curve on it is the same 5.6 mm: the cap
concentric with the lens, the cap concentric with the button, and the outer
corner, which is centred on the piezo. That is as round as the outside of an
arm this thick can be. The inside of the elbow is filleted at 4 mm, and that
fillet is measured as a true distance rather than stuck on as its own piece of
material: the boss is extruded from this field and its edge is broken by it, so
a false zero in the field comes out as a real crease in the round.

The piezo takes no part in the outline. It is far smaller than the arm is
thick, so it sits in the corner without moving an edge.

All three parts are sunk into the L's face rather than standing on it. The L
itself has no colour of its own — it is the shell, raised — and it is blended
into the back rather than stepped off it: the union with the slab is a smooth
one, so the L grows out of the shell on a 1.8 mm fillet and the whole back
reads as a single moulding. What makes it read at sixteen dither levels is that
blend, which turns through every shade between the two faces, plus the contact
shading at its foot and the walls of the three wells. Only the piezo hole and the lens glass
are actually dark, because one is a hole and the other is glass.

 In the shader, seen from behind, `+x` runs to the viewer's left; `P_LENS`,
`P_PIEZO`, `P_SHUT`, `ARM` and the `R_*` radii next to them are the whole
layout, in units where the device is one wide.

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
them. Distance is measured against the half-diagonal, so on a wide window the
ramp reaches the top and bottom edges only towards the corners.
`VIG` and `VIG_IN` at the top of the shader set the two numbers, and
`paintVignette()` repeats them for the page behind.

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

The credit under the headline — "(a project by amaranth studio)", linking to
amaranthstudio.com — is set in Book rather than Bold and at a little over a
quarter of the headline's size, so it reads as a footnote rather than a fourth
line. It carries no underline and no colour of its own.

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
