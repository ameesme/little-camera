# lttl.cam

The teaser page: `index.html` plus `camera-element.js`, no build, no dependencies, firmware screens inlined as base64. `DEPLOYMENT.md` covers how it is served.

`camera-element.js` is the device on its own, as a `<little-camera>` custom element: the shader, the screens and `scene()`/`spin()`/`celebrate()`/`morph()`. `morph()` turns the camera into the phone its pictures end up on and back, changing the body mid-turn while the silhouette is edge-on; `strip="media/blog-strip.png"` is the tall picture of the blog that scrolls behind its glass, and without one the phone's screen is blank, which is how the app gets this file without the asset. The iOS app loads the same file (`app/ios/LittleCamera/Resources/device.html`), which is the whole reason it is a file and not an inline script. It stays a classic script, not a module, because the page's own script runs at parse time and needs the element defined before it.

The device on it is a **placeholder** at the real dimensions: 57.6 × 34.56 mm of glass plus 3 mm of bezel all round gives a 63.6 × 40.6 × 7 mm body. It is raymarched in WebGL and dithered with the firmware's own Bayer matrix, so the page renders it the way the camera renders the world. Every proportion is derived from that rectangle, so `--w`, `--h`, `--d` and `--r` are enough to change it, and the whole device gets replaced when the industrial design lands. If WebGL fails to start, a CSS device still in the markup takes over.

## Remaking the assets

The photographic screens are real frames from the firmware's preview tool (`viewfinder --src`, `gallery --pbm`, over `packages/pbm/fixtures/`). Render at 400×240 and replace the matching base64 string in `SCREENS` in `camera-element.js`. The rest are generated, and the generators write straight into that object:

- `tools/gen_faces.py` draws the four sleeping faces (`sleep`, `fine`, `glum`, `sad`, each with its `A`/`B` breath frame) from the geometry in `firmware/src/display.cpp`, and asserts its band-3 output matches the committed `sleepA`/`sleepB` so the art cannot drift from the panel's. A scene key ending in `A` or `B` is what makes the element breathe.
- `tools/gen_icons.py` lifts `mail`, `send`, `images` and `camera` straight out of `firmware/src/icons.h`, so the app's screens are the camera's own Lucide icons rather than a second set drawn by hand.


```
cd firmware/tools/preview && make
./preview gallery --pbm <photo>.pbm --index 2 --total 12 --scale 1 --out out/gallery.bmp
```

`media/ground.mp4` is the film behind the headline: H.264, 540×926, 24fps, ~790 KB, and it comes first because every iOS Safari decodes it. `ground.webm` is VP9 for builds without the proprietary codecs.

```
ffmpeg -i <clip>.mov -an -vf "fps=24,scale=540:-2:flags=area" \
  -c:v libx264 -profile:v main -pix_fmt yuv420p -crf 30 -preset slow \
  -movflags +faststart site/media/ground.mp4
```

`media/og.png` is a real frame of the page at 1200×630, one device pixel per CSS pixel so the dither survives. Remake it with `tools/og-card.js`.

## Gotchas

- **Safari needs `mix-blend-mode: overlay` on the wash.** A bare `<video>` is hardware-composited, and WebKit then paints it without the opacity or the mask, so the wash ran at full strength across the middle. A blend mode forces the subtree to be rendered instead. It is scoped to WebKit, because overlay screens against the 221 paper and made Firefox visibly brighter. Don't remove it without opening the page in Safari.
- **The `brightness(.867)` on that threshold is there to cap the white.** Without it a white pixel composites to 226 against 221 paper, so it comes out brighter than the thing it is meant to shade.
- **`SHADE_MAX` sets how many dots land, so it does not scale like a grey.** At 0.72 the first row under the box comes out 72 % covered, which on a 1px dither is a solid black rule, and it hides the curve of the corners underneath.
- **Both canvases take the box size rounded up.** Get it wrong in either and the two dot lattices interleave instead of coinciding, doubling the density where they overlap.
- **The headline is measured and fitted at runtime.** iOS renders Helvetica Neue, whose Bold is fractionally wider, so sizes predicted from font metrics wrapped the longest line and three lines became four. The `0.168` in the stylesheet is only the fallback until the script runs.
- **The badge spins a `<path>` inside a `<svg>` that never moves.** Rotating the element itself swings its box out to √2 of its width, which on a 393px phone gives the page a horizontal scrollbar for part of every revolution.
- **`.sheet` carries no `z-index` on purpose.** One there traps the headline's difference blend inside the sheet instead of letting it reach the ground.
- Autoplay is refused in low power mode even muted and inline, so the first touch starts both videos and nothing depends on them playing.

## Type and colour

PP Neue Montreal and `#ddd` on `#000`, both from amaranthstudio.com so the teaser sits in the same world, with the headline lower case because every heading there is. The studio's accent `#f2385a` and its dark ground `#111` go deliberately unused, since this project is black and white and the page is light only. Everything else follows the micro-blog's tokens (`server/src/views/layout.tsx`). The font files are licensed, see `fonts/README.md`.
