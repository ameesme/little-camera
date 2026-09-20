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

## Design

Tokens and hairlines are the micro-blog's (`server/src/views/layout.tsx`): ink
on paper, Helvetica, 1px rules. The headline is the one deliberate departure
from the blog's tiny tracked caps, because at display size Helvetica Bold wants
tight tracking rather than wide. The page is black and white; the only greys
belong to the device, which has to be a solid object in order to float.

The body is extruded as a stack of 32 identical rounded rectangles rather than
six flat faces. Six faces leave notches wherever a square side meets a rounded
corner, which is what made the edges look broken; every slice here shares one
silhouette, so the outline stays continuous through the whole turn. Front slice
is lightest and back darkest, which is all the shading the edge needs.

One measure drives the whole composition. `--sheet` is the block's width: as
wide as the screen allows, capped at 480px and at 54vh so the pair stays whole
above the fold on a short laptop. Everything else is a fraction of it, so the
composition scales as one object and can never be wider than the screen:

| Part | Size |
|---|---|
| headline | `--sheet` x 0.18 |
| camera | `--sheet` x 0.63 |
| gap between them | `--sheet` x 0.12 |

The headline factor is set so its longest line fills the measure exactly:
"IS COMING." is 5.49 em, and Helvetica Bold and Arial Bold share metrics, so
the fit holds on the fallback too. The three lines are hard breaks, not
wrapping, so they hold at every width. The block is centred on the page with
the text left aligned inside it, and the camera centred beneath.
