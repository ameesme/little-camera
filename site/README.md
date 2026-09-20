# lttl.cam

The teaser page. One file, no build, no dependencies: `index.html` is
self-contained (the firmware screens are inlined as base64 PNGs), so it can be
dropped on any static host or served by Caddy with a two-line site block.

The device is a placeholder: a rounded grey slab at roughly the real
dimensions. The 2.7" 400x240 Sharp panel is 57.6 x 34.56 mm of glass; 1.75 mm
of bezel at the sides and 3 mm top and bottom puts the whole thing at
61.1 x 40.6 x 7 mm. The front is all screen, with no button on it. The shell's
corner radius is 3.5 mm and the glass radius is that less the bezel, so the two
curves stay concentric. Replace it when the industrial design lands; every
proportion is derived from that rectangle, so changing `--w`, `--h`, `--d` and
`--r` at the top of the stylesheet is enough.

## The back

Three things live on an L-shaped area on the back, and the L is why the top and
bottom bezels are wider than the sides:

| Part | Where |
|---|---|
| lens | the geometric centre of the back |
| piezo hole | at the left edge (seen from behind), on the same centre line |
| shutter | directly above the piezo, in the top-left corner |

The foot of the L runs from the lens out to the piezo, which is half the width
of the device; the upright rises from there to the shutter. The two arms cannot
be the same length — half the width is more than the device is tall — so the L
is not diagonally symmetric. Both arms are the same 11 mm thickness, which is
what makes it read as one shape.

It is drawn flat rather than raised. An extrusion shares its surface normal
with the panel it stands on, so at sixteen dither levels the two shade
identically and the shape vanishes; and for the same reason every part is
outlined as well as filled, because a hard step survives the dither where a
difference in tone does not.

In the shader, seen from behind, `+x` runs to the viewer's left; `P_LENS`,
`P_PIEZO`, `P_SHUT` and the `R_*` radii next to them are the whole layout, in
units where the device is one wide.

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
two levels are the page's own ink and ground, so the canvas has no edge: its
paper is exactly the `#ddd` behind it. The dither is
the firmware's own 4x4 Bayer matrix (`BAYER4` in `display.cpp`), applied in
screen space at one dot per CSS pixel, which on a phone lands close to the
physical pixel pitch of the real 400x240 panel. Output is strictly black and
white: no intermediate values reach the canvas.

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
wide as the screen allows, capped at 480px and at 54svh so the pair stays whole
on one screen. The stack is about 1.44 measures tall, so that height cap is
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
