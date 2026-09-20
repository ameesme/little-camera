# lttl.cam

The teaser page. One file, no build, no dependencies: `index.html` is
self-contained (the firmware screens are inlined as base64 PNGs), so it can be
dropped on any static host or served by Caddy with a two-line site block.

The device is a placeholder: a rounded grey slab at roughly the real
dimensions. The 2.7" 400x240 Sharp panel is 57.6 x 34.56 mm of glass, and a
1.75 mm bezel all round puts the whole thing at 61.1 x 38.1 x 7 mm. It is all
screen: no button, and one small lens on the back. The shell's corner radius
is 3.5 mm and the glass radius is that less the bezel, so the two curves stay
concentric. Replace it when the
industrial design lands; every proportion is derived from that rectangle, so
changing `--w`, `--h`, `--d` and `--r` at the top of the stylesheet is enough.

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
tracked capitals are right at 10px and wrong at 90.

The font files are licensed and are not in the repository; see `fonts/README.md`.
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
wide as the screen allows, capped at 480px and at 54vh so the pair stays whole
above the fold on a short laptop. Everything else is a fraction of it, so the
composition scales as one object and can never be wider than the screen:

| Part | Size |
|---|---|
| headline | `--sheet` x 0.18 |
| camera | `--sheet` x 0.76 |
| gap between them | `--sheet` x 0.12 |

The headline is fitted at runtime rather than calculated. Predicting the line
width from font metrics was wrong on real devices: iOS renders Helvetica Neue,
whose Bold is fractionally wider than Helvetica or Arial, so the longest line
wrapped there and the three lines became four. The script now measures the
lines in whatever font actually resolved and sets the size so the widest one
fills the measure exactly. The 0.168 factor in the stylesheet is only the
pre-script fallback, deliberately short of the measure.

Each line is its own block with `white-space: nowrap`, so three lines stay
three lines whatever the font does. The block is centred horizontally with the text
left aligned inside it and the camera centred beneath, and sits high on the
page rather than in the middle, so the headline leads.
